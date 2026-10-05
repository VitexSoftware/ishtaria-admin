#include "ishtariaadmin/App.h"
#include "ishtariaadmin/i18n.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

int main(int argc, char **argv) {
    ishtariaadmin::initI18n();
    std::string url = "postgresql:///ishtaria?host=/var/run/postgresql";
    if (const char *env = std::getenv("DATABASE_URL")) {
        url = env;
    }
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: ishtaria-admin [--database-url=URL]\n"
                         "Default: $DATABASE_URL or postgresql:///ishtaria?host=/var/run/postgresql\n"
                         "Run as the ishtaria user: sudo -u ishtaria ishtaria-admin\n";
            return 0;
        }
        if (arg == "--version") {
            std::cout << "ishtaria-admin " ISHTARIA_ADMIN_VERSION "\n";
            return 0;
        }
        if (arg.rfind("--database-url=", 0) == 0) {
            url = arg.substr(std::strlen("--database-url="));
        }
    }
    return ishtariaadmin::runApp(url);
}
