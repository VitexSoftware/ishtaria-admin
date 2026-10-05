#pragma once

#include <libintl.h>

namespace ishtariaadmin {
void initI18n();
}

#define _(text) ::gettext(text)
