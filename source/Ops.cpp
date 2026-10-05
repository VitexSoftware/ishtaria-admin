#include "ishtariaadmin/Ops.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <libpq-fe.h>
#include <regex>
#include <sys/wait.h>

namespace ishtariaadmin {

namespace {
std::string str(long v) { return std::to_string(v); }
} // namespace

long Ops::worldId() {
    const auto rows = db_.exec("SELECT id FROM worlds ORDER BY id LIMIT 1");
    if (rows.empty()) {
        throw OpError("The world is not initialised yet (run ishtaria-server once).");
    }
    return std::stol(rows[0][0]);
}

bool Ops::validUsername(const std::string &name) {
    static const std::regex re("^[a-zA-Z0-9_-]{3,32}$");
    return std::regex_match(name, re);
}

// --- maps -------------------------------------------------------------------

Rows Ops::listMaps() {
    return db_.exec("SELECT id::text, name, seed, face_size::text, left(sha256, 12), "
                    "to_char(created_at, 'YYYY-MM-DD HH24:MI') FROM world_maps ORDER BY name");
}

Row Ops::activeMap() {
    const auto rows = db_.exec("SELECT seed, face_size::text, left(sha256, 12) FROM heightmaps LIMIT 1");
    return rows.empty() ? Row{"", "", ""} : rows[0];
}

void Ops::saveActiveMap(const std::string &name) {
    if (name.empty() || name.size() > 64) {
        throw OpError("The map name must have 1-64 characters.");
    }
    db_.exec("INSERT INTO world_maps (world_id, name, seed, face_size, sha256, pgm, pixels) "
             "SELECT world_id, $1, seed, face_size, sha256, pgm, pixels FROM heightmaps",
             {name});
    if (db_.affected() == 0) {
        throw OpError("No map is imported in the world yet.");
    }
}

void Ops::loadMap(long id, bool force) {
    Transaction tx(db_);
    db_.exec("SELECT id FROM worlds WHERE id = $1 FOR UPDATE", {str(worldId())});
    const auto alive = db_.exec("SELECT count(*) FROM players WHERE died_at IS NULL");
    if (!force && alive[0][0] != "0") {
        throw OpError("The world has " + alive[0][0] + " living players; loading a map needs confirmation.");
    }
    db_.exec("UPDATE heightmaps SET seed = m.seed, face_size = m.face_size, sha256 = m.sha256, "
             "pgm = m.pgm, pixels = m.pixels, imported_at = now() "
             "FROM world_maps m WHERE m.id = $1 AND heightmaps.world_id = m.world_id",
             {str(id)});
    if (db_.affected() == 0) {
        // No active map yet: take it from the library.
        db_.exec("INSERT INTO heightmaps (world_id, seed, face_size, sha256, pgm, pixels) "
                 "SELECT world_id, seed, face_size, sha256, pgm, pixels FROM world_maps WHERE id = $1",
                 {str(id)});
        if (db_.affected() == 0) {
            throw OpError("The map no longer exists.");
        }
    }
    tx.commit();
}

void Ops::renameMap(long id, const std::string &name) {
    if (name.empty() || name.size() > 64) {
        throw OpError("The map name must have 1-64 characters.");
    }
    db_.exec("UPDATE world_maps SET name = $2 WHERE id = $1", {str(id), name});
    if (db_.affected() == 0) {
        throw OpError("The map no longer exists.");
    }
}

void Ops::deleteMap(long id) {
    db_.exec("DELETE FROM world_maps WHERE id = $1", {str(id)});
    if (db_.affected() == 0) {
        throw OpError("The map no longer exists.");
    }
}

void Ops::exportMap(long id, const std::string &path) {
    // bytea comes back hex-encoded in text format; decode it ourselves.
    const auto rows = db_.exec("SELECT encode(pgm, 'hex') FROM world_maps WHERE id = $1", {str(id)});
    if (rows.empty()) {
        throw OpError("The map no longer exists.");
    }
    // Create the file exclusively and never follow a symlink, so an export
    // cannot be redirected onto another file (the tool runs as the service user).
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0640);
    if (fd < 0) {
        throw OpError("Cannot create " + path + ": " + std::strerror(errno) +
                      " (an existing file is never overwritten)");
    }
    const std::string &hex = rows[0][0];
    std::string bytes;
    bytes.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        bytes.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    std::size_t written = 0;
    while (written < bytes.size()) {
        const ssize_t n = ::write(fd, bytes.data() + written, bytes.size() - written);
        if (n < 0) {
            const int err = errno;
            ::close(fd);
            ::unlink(path.c_str());
            throw OpError(std::string("Cannot write ") + path + ": " + std::strerror(err));
        }
        written += static_cast<std::size_t>(n);
    }
    if (::close(fd) != 0) {
        throw OpError("Cannot write " + path);
    }
}

namespace {

constexpr std::size_t kMaxMapBytes = 16 * 1024 * 1024;

// Runs the generator without a shell and collects its standard output.
std::string runGenerator(const std::string &program, const std::vector<std::string> &args) {
    int fds[2];
    if (::pipe(fds) != 0) {
        throw OpError("Cannot create a pipe.");
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(fds[0]);
        ::close(fds[1]);
        throw OpError("Cannot start the generator.");
    }
    if (pid == 0) {
        ::dup2(fds[1], STDOUT_FILENO);
        ::close(fds[0]);
        ::close(fds[1]);
        std::vector<char *> argv;
        argv.push_back(const_cast<char *>(program.c_str()));
        for (const auto &a : args) {
            argv.push_back(const_cast<char *>(a.c_str()));
        }
        argv.push_back(nullptr);
        ::execvp(program.c_str(), argv.data());
        ::_exit(127);
    }
    ::close(fds[1]);
    std::string out;
    char buffer[65536];
    bool tooBig = false;
    for (;;) {
        const ssize_t n = ::read(fds[0], buffer, sizeof(buffer));
        if (n <= 0) {
            break;
        }
        if (out.size() + static_cast<std::size_t>(n) > kMaxMapBytes) {
            tooBig = true;
            break;
        }
        out.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(fds[0]);
    if (tooBig) {
        ::kill(pid, SIGKILL);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    if (tooBig) {
        throw OpError("The generated map is larger than 16 MiB.");
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw OpError(WIFEXITED(status) && WEXITSTATUS(status) == 127
                          ? program + " is not installed (package ishtaria-worldgen)."
                          : "The generator failed.");
    }
    return out;
}

} // namespace

void Ops::generateMap(const std::string &name, unsigned long long seed, int faceSize) {
    if (name.empty() || name.size() > 64) {
        throw OpError("The map name must have 1-64 characters.");
    }
    if (faceSize < 16 || faceSize > 1024) {
        throw OpError("The face size must be 16-1024.");
    }
    const char *override_path = std::getenv("ISHTARIA_WORLDGEN");
    const std::string pgm = runGenerator(override_path != nullptr ? override_path : "ishtaria-worldgen",
                                         {std::to_string(seed), std::to_string(faceSize)});
    // Validate the P5 header exactly as the server expects it before storing anything.
    std::size_t pos = 0;
    auto token = [&]() {
        while (pos < pgm.size() && std::isspace(static_cast<unsigned char>(pgm[pos]))) {
            ++pos;
        }
        const std::size_t start = pos;
        while (pos < pgm.size() && !std::isspace(static_cast<unsigned char>(pgm[pos]))) {
            ++pos;
        }
        return pgm.substr(start, pos - start);
    };
    if (token() != "P5") {
        throw OpError("The generator did not produce a binary PGM.");
    }
    const long w = std::atol(token().c_str());
    const long h = std::atol(token().c_str());
    const std::string maxval = token();
    ++pos; // single whitespace after maxval
    if (w != 6L * faceSize || h != faceSize || maxval != "255" ||
        pgm.size() - std::min(pos, pgm.size()) != static_cast<std::size_t>(w * h)) {
        throw OpError("The generated map has an unexpected format.");
    }
    static const char *digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(pgm.size() * 2);
    for (unsigned char c : pgm) {
        hex.push_back(digits[c >> 4]);
        hex.push_back(digits[c & 15]);
    }
    db_.exec("INSERT INTO world_maps (world_id, name, seed, face_size, sha256, pgm, pixels) "
             "SELECT $1::bigint, $2, $3, $4::int, encode(sha256(decode($5, 'hex')), 'hex'), "
             "decode($5, 'hex'), substring(decode($5, 'hex') FROM $6::int + 1)",
             {std::to_string(worldId()), name, std::to_string(seed), std::to_string(faceSize), hex,
              std::to_string(pos)});
}

// --- players ------------------------------------------------------------------

Rows Ops::listPlayers() {
    return db_.exec("SELECT id::text, username, "
                    "CASE WHEN banned_at IS NOT NULL THEN 'banned' WHEN died_at IS NOT NULL THEN 'dead' ELSE 'alive' END, "
                    "coalesce((SELECT quantity FROM player_inventory WHERE player_id = players.id AND item_id = 'gold'), 0)::text, "
                    "to_char(created_at, 'YYYY-MM-DD') FROM players ORDER BY lower(username), id");
}

void Ops::renamePlayer(long id, const std::string &name) {
    if (!validUsername(name)) {
        throw OpError("Names use 3-32 characters: letters, digits, '_' and '-'.");
    }
    db_.exec("UPDATE players SET username = $2 WHERE id = $1", {str(id), name});
    if (db_.affected() == 0) {
        throw OpError("The player no longer exists.");
    }
}

void Ops::banPlayer(long id, const std::string &reason) {
    Transaction tx(db_);
    db_.exec("UPDATE players SET banned_at = now(), ban_reason = $2 WHERE id = $1",
             {str(id), reason.substr(0, 500)});
    if (db_.affected() == 0) {
        throw OpError("The player no longer exists.");
    }
    db_.exec("DELETE FROM player_sessions WHERE player_id = $1", {str(id)});
    tx.commit();
}

void Ops::unbanPlayer(long id) {
    db_.exec("UPDATE players SET banned_at = NULL, ban_reason = NULL WHERE id = $1", {str(id)});
}

void Ops::deletePlayer(long id) {
    const auto grave = db_.exec("SELECT 1 FROM graves WHERE player_id = $1", {str(id)});
    if (!grave.empty()) {
        throw OpError("The player has a permanent memorial and cannot be deleted. Ban the account instead.");
    }
    Transaction tx(db_);
    db_.exec("DELETE FROM player_friendships WHERE player_id = $1 OR friend_id = $1", {str(id)});
    db_.exec("DELETE FROM players WHERE id = $1", {str(id)});
    if (db_.affected() == 0) {
        throw OpError("The player no longer exists.");
    }
    tx.commit();
}

// --- portals ------------------------------------------------------------------

Rows Ops::listPortals() {
    return db_.exec("SELECT id::text, name, coalesce(peer, ''), state, face::text, x::text, y::text "
                    "FROM portals ORDER BY name");
}

void Ops::createPortal(const std::string &name, const std::string &peer, int face, int x, int y) {
    static const std::regex nameRe("^[a-z0-9-]{1,64}$");
    static const std::regex peerRe("^[a-z0-9.-]{1,253}$");
    if (!std::regex_match(name, nameRe)) {
        throw OpError("Portal name: 1-64 characters a-z, 0-9 and '-'.");
    }
    if (!peer.empty() && !std::regex_match(peer, peerRe)) {
        throw OpError("The peer must be a lowercase DNS name.");
    }
    if (face < 0 || face > 5 || x < 0 || y < 0) {
        throw OpError("Face is 0-5, coordinates must not be negative.");
    }
    db_.exec("INSERT INTO portals (world_id, name, peer, face, x, y) VALUES ($1, $2, $3, $4, $5, $6)",
             {str(worldId()), name, peer.empty() ? Param{} : Param{peer}, str(face), str(x), str(y)});
}

void Ops::setPortalState(long id, const std::string &state) {
    db_.exec("UPDATE portals SET state = $2, updated_at = now() WHERE id = $1", {str(id), state});
    if (db_.affected() == 0) {
        throw OpError("The portal no longer exists.");
    }
}

void Ops::deletePortal(long id) {
    db_.exec("DELETE FROM portals WHERE id = $1", {str(id)});
    if (db_.affected() == 0) {
        throw OpError("The portal no longer exists.");
    }
}

// --- server -------------------------------------------------------------------

Row Ops::scheduledShutdown() {
    const auto rows = db_.exec("SELECT GREATEST(0, CEIL(EXTRACT(EPOCH FROM shutdown_at - now())))::bigint::text, "
                               "coalesce(message, '') FROM server_shutdown");
    return rows.empty() ? Row{} : rows[0];
}

void Ops::scheduleShutdown(int delaySeconds, const std::string &message) {
    if (delaySeconds < 0 || delaySeconds > 86400) {
        throw OpError("The delay must be 0-86400 seconds.");
    }
    if (message.size() > 200) {
        throw OpError("The message may have at most 200 bytes.");
    }
    db_.exec("INSERT INTO server_shutdown (world_id, shutdown_at, message) "
             "VALUES ($1, now() + make_interval(secs => $2::int), $3) "
             "ON CONFLICT (world_id) DO UPDATE SET requested_at = now(), "
             "shutdown_at = EXCLUDED.shutdown_at, message = EXCLUDED.message, requested_by = current_user",
             {str(worldId()), str(delaySeconds), message.empty() ? Param{} : Param{message}});
}

void Ops::cancelShutdown() { db_.exec("DELETE FROM server_shutdown"); }

} // namespace ishtariaadmin
