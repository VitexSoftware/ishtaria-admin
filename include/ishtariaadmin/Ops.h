#pragma once

#include "ishtariaadmin/Db.h"

#include <string>

namespace ishtariaadmin {

// Administrative operations on the ishtaria-server database. Every function
// throws DbError (database problem) or OpError (refused by a safety rule).
struct OpError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Ops {
public:
    explicit Ops(Db &db) : db_(db) {}

    // --- world maps -------------------------------------------------------
    // Rows: id, name, seed, face_size, sha256 prefix, created_at
    Rows listMaps();
    // Row: seed, face_size, sha256 prefix (empty when no map is imported).
    Row activeMap();
    void saveActiveMap(const std::string &name);
    // Replaces the active map. Refused while living players exist unless force.
    void loadMap(long id, bool force);
    void renameMap(long id, const std::string &name);
    void deleteMap(long id);
    void exportMap(long id, const std::string &path);

    // --- players ----------------------------------------------------------
    // Rows: id, username, state (alive/dead/banned), gold, created_at
    Rows listPlayers();
    void renamePlayer(long id, const std::string &name);
    void banPlayer(long id, const std::string &reason);
    void unbanPlayer(long id);
    void deletePlayer(long id);

    // --- linked worlds ------------------------------------------------------
    // Rows: id, name, peer, state, face, x, y
    Rows listPortals();
    void createPortal(const std::string &name, const std::string &peer, int face, int x, int y);
    void setPortalState(long id, const std::string &state);
    void deletePortal(long id);

    // --- server -------------------------------------------------------------
    // Row: seconds_left, message (empty when nothing is scheduled).
    Row scheduledShutdown();
    void scheduleShutdown(int delaySeconds, const std::string &message);
    void cancelShutdown();

    static bool validUsername(const std::string &name);

private:
    long worldId();
    Db &db_;
};

} // namespace ishtariaadmin
