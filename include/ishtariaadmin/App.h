#pragma once

#include <string>

namespace ishtariaadmin {
// Runs the Turbo Vision application; returns the process exit code.
int runApp(const std::string &databaseUrl);
} // namespace ishtariaadmin
