#pragma once
#include <QString>

// Installs a global crash handler that:
//  - Catches SIGSEGV / SIGABRT / SIGBUS / SIGFPE / SIGILL (Unix)
//    or Windows unhandled SEH exceptions
//  - Catches unhandled C++ exceptions via std::set_terminate()
//  - Prints an annotated stack trace (with file:line where debug info is available)
//    to stderr AND writes it to a timestamped log file
//
// Call CrashHandler::install() once, before QApplication::exec().
//
// macOS: uses `atos` to resolve addresses to source:line (requires Xcode CLI tools
//        or a build with DWARF debug info). Falls back to backtrace_symbols() + demangling.
// Windows: uses DbgHelp StackWalk64 + SymGetLineFromAddr64 (requires PDB next to the .exe).
// Linux:  uses backtrace_symbols() + abi::__cxa_demangle.

namespace CrashHandler {
    // logDir: directory where crash-YYYY-MM-DDTHH-MM-SS.log files are saved.
    //         Defaults to QStandardPaths::AppDataLocation + "/crashes".
    void install(const QString &logDir = QString());
}
