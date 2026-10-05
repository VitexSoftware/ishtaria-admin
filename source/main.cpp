#include "ishtariaadmin/App.h"
#include "ishtariaadmin/i18n.h"

#include <cstdlib>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

namespace {

// The default connection uses peer authentication over the local socket, which
// maps the OS user to a database role of the same name. When started as root,
// continue as the service user instead of failing with 'role "root" does not exist'.
bool dropToServiceUser() {
    if (geteuid() != 0) {
        return true;
    }
    const passwd *service = getpwnam("ishtaria");
    if (service == nullptr) {
        return true; // no service user on this host: keep going, the connection error explains
    }
    if (initgroups("ishtaria", service->pw_gid) != 0 || setgid(service->pw_gid) != 0 ||
        setuid(service->pw_uid) != 0 || geteuid() == 0) {
        std::fprintf(stderr, "ishtaria-admin: cannot switch to user ishtaria\n");
        return false;
    }
    setenv("HOME", service->pw_dir, 1);
    if (chdir("/") != 0) {
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char **argv) {
    ishtariaadmin::initI18n();
    std::string url = "postgresql:///ishtaria?host=/var/run/postgresql";
    bool explicitUrl = false;
    if (const char *env = std::getenv("DATABASE_URL")) {
        url = env;
        explicitUrl = true;
    }
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: ishtaria-admin [--database-url=URL]\n"
                         "Default: $DATABASE_URL or postgresql:///ishtaria?host=/var/run/postgresql\n"
                         "Started as root it continues as the ishtaria user (when no URL is given).\n";
            return 0;
        }
        if (arg == "--version") {
            std::cout << "ishtaria-admin " ISHTARIA_ADMIN_VERSION "\n";
            return 0;
        }
        if (arg.rfind("--database-url=", 0) == 0) {
            url = arg.substr(std::strlen("--database-url="));
            explicitUrl = true;
        }
    }
    if (!explicitUrl && !dropToServiceUser()) {
        return 1;
    }
    return ishtariaadmin::runApp(url);
}
