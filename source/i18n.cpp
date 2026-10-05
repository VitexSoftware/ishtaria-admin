#include "ishtariaadmin/i18n.h"

#include <clocale>

namespace ishtariaadmin {

void initI18n() {
    std::setlocale(LC_ALL, "");
    bindtextdomain("ishtaria-admin", ISHTARIA_ADMIN_LOCALEDIR);
    bind_textdomain_codeset("ishtaria-admin", "UTF-8");
    textdomain("ishtaria-admin");
}

} // namespace ishtariaadmin
