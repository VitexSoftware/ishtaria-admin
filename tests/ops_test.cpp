// Exercises Ops against a throw-away database prepared by run_ops_test.sh.
#include "ishtariaadmin/Ops.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>

using namespace ishtariaadmin;

static int failures = 0;
#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::cerr << "FAILED " << __LINE__ << ": " #cond "\n";                       \
            ++failures;                                                                  \
        }                                                                                \
    } while (0)

template <typename F> static bool throwsOp(F f) {
    try {
        f();
    } catch (const OpError &) {
        return true;
    }
    return false;
}

int main() {
    const char *url = std::getenv("DATABASE_URL");
    if (url == nullptr) {
        std::cerr << "DATABASE_URL is required\n";
        return 77;
    }
    Db db(url);
    Ops ops(db);

    // maps
    CHECK(!ops.activeMap()[0].empty());
    ops.saveActiveMap("first");
    CHECK(ops.listMaps().size() == 1);
    CHECK(throwsOp([&] { ops.saveActiveMap(""); }));
    bool duplicate = false;
    try {
        ops.saveActiveMap("first");
    } catch (const DbError &) {
        duplicate = true;
    }
    CHECK(duplicate);
    const long mapId = std::stol(ops.listMaps()[0][0]);
    ops.renameMap(mapId, "renamed");
    CHECK(ops.listMaps()[0][1] == "renamed");
    std::remove("ops_test_export.pgm");
    ops.exportMap(mapId, "ops_test_export.pgm");
    CHECK(throwsOp([&] { ops.exportMap(mapId, "ops_test_export.pgm"); })); // never overwrites
    CHECK(symlink("ops_test_export.pgm", "ops_test_link.pgm") == 0);
    CHECK(throwsOp([&] { ops.exportMap(mapId, "ops_test_link.pgm"); })); // never follows a symlink
    std::remove("ops_test_link.pgm");
    std::ifstream pgm("ops_test_export.pgm", std::ios::binary);
    std::string magic(2, '\0');
    pgm.read(magic.data(), 2);
    CHECK(magic == "P5");
    ops.loadMap(mapId, false); // no living players yet
    std::remove("ops_test_export.pgm");

    // players: created through SQL, as the server does on registration
    db.exec("INSERT INTO players (world_id, username, password_hash) SELECT id, 'admin-test', 'x' FROM worlds");
    db.exec("INSERT INTO players (world_id, username, password_hash) SELECT id, 'second-test', 'x' FROM worlds");
    CHECK(throwsOp([&] { ops.loadMap(mapId, false); })); // living players now
    ops.loadMap(mapId, true);
    const long pid = std::stol(db.exec("SELECT id FROM players WHERE username = 'admin-test'")[0][0]);
    CHECK(throwsOp([&] { ops.renamePlayer(pid, "no"); }));
    ops.renamePlayer(pid, "admin-renamed");
    db.exec("INSERT INTO player_sessions (token_hash, player_id) VALUES (decode(repeat('ab', 32), 'hex'), $1)", {std::to_string(pid)});
    ops.banPlayer(pid, "test");
    CHECK(db.exec("SELECT count(*) FROM player_sessions WHERE player_id = $1", {std::to_string(pid)})[0][0] == "0");
    CHECK(ops.listPlayers()[0][2] == "banned");
    ops.unbanPlayer(pid);
    ops.deletePlayer(pid);
    CHECK(ops.listPlayers().size() == 1);

    // portals
    ops.createPortal("gate-one", "other.example.org", 1, 10, 20);
    CHECK(throwsOp([&] { ops.createPortal("Bad Name", "", 0, 0, 0); }));
    const long portal = std::stol(ops.listPortals()[0][0]);
    CHECK(ops.listPortals()[0][3] == "building");
    for (const char *state : {"open", "closed", "disabled", "banned"}) {
        ops.setPortalState(portal, state);
        CHECK(ops.listPortals()[0][3] == state);
    }
    ops.deletePortal(portal);
    CHECK(ops.listPortals().empty());

    // shutdown
    CHECK(ops.scheduledShutdown().empty());
    ops.scheduleShutdown(60, "Maintenance");
    CHECK(ops.scheduledShutdown()[1] == "Maintenance");
    ops.cancelShutdown();
    CHECK(ops.scheduledShutdown().empty());
    CHECK(throwsOp([&] { ops.scheduleShutdown(-1, ""); }));

    ops.deleteMap(mapId);
    CHECK(ops.listMaps().empty());

    if (failures == 0) {
        std::cout << "all ops tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
