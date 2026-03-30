#include "CrashHandler.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>
#include <stdarg.h>

// ── Static state set once at install() time ───────────────────────────────────
// Plain C strings so they are safe to read from async signal handlers.
static char g_logDir[2048]  = {};
static char g_binPath[2048] = {};
static char g_version[64]   = {};

// ── Report writer — used by both Unix and Windows paths ───────────────────────
//
// Up to two output sinks: stderr and (optionally) a log file.
// Populated by each platform's crash entry point before calling reportLine().
static FILE *g_sinks[2];
static int   g_sinkCount = 0;

static void reportLine(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    for (int i = 0; i < g_sinkCount; ++i) {
        fputs(buf, g_sinks[i]);
        fputc('\n', g_sinks[i]);
        fflush(g_sinks[i]);
    }
}

// Open log file and populate g_sinks[].  Returns the FILE* (or nullptr).
static FILE *openSinks(const char *timeTag, char *pathBufOut, size_t pathBufLen)
{
    g_sinks[0]   = stderr;
    g_sinkCount  = 1;

    FILE *log = nullptr;
    if (g_logDir[0]) {
        snprintf(pathBufOut, pathBufLen, "%s/crash-%s.log", g_logDir, timeTag);
        log = fopen(pathBufOut, "w");
        if (log) {
            g_sinks[g_sinkCount++] = log;
        } else {
            pathBufOut[0] = '\0'; // signal that open failed
        }
    }
    return log;
}

// ══════════════════════════════════════════════════════════════════════════════
//  Unix (macOS + Linux)
// ══════════════════════════════════════════════════════════════════════════════
#if defined(Q_OS_UNIX)

#include <execinfo.h>
#include <signal.h>
#include <cxxabi.h>
#include <sys/utsname.h>
#include <unistd.h>

#if defined(Q_OS_MACOS)
#  include <mach-o/dyld.h>
#endif

static const char *sigDescription(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV — Segmentation fault (invalid memory access)";
    case SIGABRT: return "SIGABRT — Abort (assertion / std::abort)";
    case SIGBUS:  return "SIGBUS  — Bus error (misaligned or unmapped memory)";
    case SIGFPE:  return "SIGFPE  — Floating-point exception (e.g. divide by zero)";
    case SIGILL:  return "SIGILL  — Illegal instruction";
    default:      return "Unknown signal";
    }
}

// Demangle a C++ symbol; returns the input unchanged on failure.
static std::string demangle(const char *sym)
{
    if (!sym || !*sym) return "(null)";
    int status = -1;
    char *d = abi::__cxa_demangle(sym, nullptr, nullptr, &status);
    if (status == 0 && d) { std::string r(d); free(d); return r; }
    return sym;
}

// ── macOS: use atos for source-level stack traces ─────────────────────────────
#if defined(Q_OS_MACOS)

static void resolveWithAtos(void **frames, int n, std::string *out)
{
    // atos lives in the Xcode CLI tools; check it's present before calling.
    if (n <= 0 || !g_binPath[0]) return;
    if (access("/usr/bin/atos", X_OK) != 0) return;

    // Load address of the main Mach-O image (the ASLR slide base).
    const struct mach_header *hdr = _dyld_get_image_header(0);
    if (!hdr) return;

    // atos -o <binary> -arch <arch> -l <load_addr> <addr0> <addr1> …
    const char *arch =
#if defined(__arm64__)
        "arm64";
#else
        "x86_64";
#endif

    char cmd[8192];
    int pos = snprintf(cmd, sizeof(cmd),
                       "/usr/bin/atos -o '%s' -arch %s -l 0x%lx",
                       g_binPath, arch, (unsigned long)hdr);

    for (int i = 0; i < n && pos < (int)sizeof(cmd) - 32; ++i)
        pos += snprintf(cmd + pos, sizeof(cmd) - pos, " 0x%lx", (unsigned long)frames[i]);

    strncat(cmd, " 2>/dev/null", sizeof(cmd) - strlen(cmd) - 1);

    FILE *f = popen(cmd, "r");
    if (!f) return;

    char line[512];
    for (int i = 0; i < n; ++i) {
        if (!fgets(line, sizeof(line), f)) break;
        size_t len = strlen(line);
        if (len && line[len-1] == '\n') line[len-1] = 0;
        out[i] = line;
    }
    pclose(f);
}

#endif // Q_OS_MACOS

// ── Core Unix report writer ────────────────────────────────────────────────────
static void writeUnixReport(int sig, const char *exMsg)
{
    // Timestamp
    time_t now = time(nullptr);
    struct tm t = {};
    localtime_r(&now, &t);
    char human[32], tag[32];
    strftime(human, sizeof(human), "%Y-%m-%d %H:%M:%S", &t);
    strftime(tag,   sizeof(tag),   "%Y-%m-%dT%H-%M-%S",  &t);

    // Collect frames first (before any allocations that might perturb things)
    static const int kMax = 64;
    void  *frames[kMax];
    int    nFrames = backtrace(frames, kMax);
    char **syms    = backtrace_symbols(frames, nFrames);

    // OS info
    struct utsname uts; uname(&uts);

    // Open sinks
    char logPath[2048] = {};
    FILE *logFile = openSinks(tag, logPath, sizeof(logPath));

    // Header
    reportLine("============================= CRASH REPORT =============================");
    reportLine("Date:        %s", human);
    reportLine("Application: %s", g_version[0] ? g_version : "StockChart");
    reportLine("OS:          %s %s  (%s)", uts.sysname, uts.release, uts.machine);
    reportLine("Binary:      %s", g_binPath);
    reportLine("");

    if (sig > 0)
        reportLine("Exception Type: Signal %d  (%s)", sig, sigDescription(sig));
    else
        reportLine("Exception Type: C++ unhandled exception (std::terminate)");
    if (exMsg && exMsg[0])
        reportLine("Exception:      %s", exMsg);

    reportLine("");
    reportLine("Thread 0 Crashed:");

#if defined(Q_OS_MACOS)
    // Try atos for rich file:line output
    std::string atosOut[kMax];
    resolveWithAtos(frames, nFrames, atosOut);

    for (int i = 0; i < nFrames; ++i) {
        if (!atosOut[i].empty() && atosOut[i].find("???") == std::string::npos) {
            // atos gives e.g. "foo(int) (in StockChart) (Foo.cpp:42)"
            reportLine("  #%-2d  %s", i, atosOut[i].c_str());
        } else {
            // Fall back: backtrace_symbols line + manual demangling
            const char *raw = syms ? syms[i] : "??";
            reportLine("  #%-2d  %s", i, raw);
        }
    }
#else
    // Linux: backtrace_symbols output: "./prog(mangled+0x1f) [0xaddr]"
    // Attempt to demangle the embedded symbol.
    for (int i = 0; i < nFrames; ++i) {
        const char *raw = syms ? syms[i] : "??";
        std::string line(raw);
        auto p1 = line.find('(');
        auto p2 = line.find('+', p1 == std::string::npos ? 0 : p1);
        if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1 + 1) {
            std::string mangled = line.substr(p1 + 1, p2 - p1 - 1);
            if (!mangled.empty())
                line = line.substr(0, p1+1) + demangle(mangled.c_str()) + line.substr(p2);
        }
        reportLine("  #%-2d  %s", i, line.c_str());
    }
#endif

    reportLine("");
    reportLine("========================================================================");
    if (logPath[0])
        reportLine("Crash log: %s", logPath);
    reportLine("========================================================================");

    free(syms);
    if (logFile) fclose(logFile);
}

// ── Signal handler & terminate handler ───────────────────────────────────────
static void signalHandler(int sig, siginfo_t *, void *)
{
    writeUnixReport(sig, nullptr);
    // Reset to default and re-raise so the OS gets the correct exit code / core.
    signal(sig, SIG_DFL);
    raise(sig);
}

static void terminateHandler()
{
    std::string msg;
    try {
        if (auto ep = std::current_exception()) {
            try { std::rethrow_exception(ep); }
            catch (const std::exception &e) { msg = e.what(); }
            catch (...)                     { msg = "(non-std::exception)"; }
        }
    } catch (...) {}

    writeUnixReport(0, msg.empty() ? nullptr : msg.c_str());
    std::abort();
}

static void platformInstall()
{
    struct sigaction sa = {};
    sa.sa_flags    = SA_SIGINFO | SA_RESETHAND;
    sa.sa_sigaction = signalHandler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS,  &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);

    std::set_terminate(terminateHandler);
}

// ══════════════════════════════════════════════════════════════════════════════
//  Windows
// ══════════════════════════════════════════════════════════════════════════════
#elif defined(Q_OS_WIN)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>

static const char *exceptionName(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
    case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
    case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
    case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
    case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
    case 0xE06D7363:                         return "C++ exception (0xE06D7363)";
    default:                                 return "Unknown";
    }
}

static void writeWindowsReport(EXCEPTION_POINTERS *ep, const char *exMsg)
{
    // Timestamp
    SYSTEMTIME st; GetLocalTime(&st);
    char human[32], tag[32];
    snprintf(human, sizeof(human), "%04d-%02d-%02d %02d:%02d:%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    snprintf(tag, sizeof(tag), "%04d-%02d-%02dT%02d-%02d-%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    // OS version
    OSVERSIONINFOW osv = {}; osv.dwOSVersionInfoSize = sizeof(osv);
#pragma warning(suppress: 4996)
    GetVersionExW(&osv);

    // Open sinks
    char logPath[2048] = {};
    FILE *logFile = openSinks(tag, logPath, sizeof(logPath));

    DWORD exCode = ep ? ep->ExceptionRecord->ExceptionCode : 0;

    reportLine("============================= CRASH REPORT =============================");
    reportLine("Date:        %s", human);
    reportLine("Application: %s", g_version[0] ? g_version : "StockChart");
    reportLine("OS:          Windows %lu.%lu (build %lu)",
               osv.dwMajorVersion, osv.dwMinorVersion, osv.dwBuildNumber);
    reportLine("Binary:      %s", g_binPath);
    reportLine("");

    if (exMsg && exMsg[0]) {
        reportLine("Exception Type: C++ unhandled exception (std::terminate)");
        reportLine("Exception:      %s", exMsg);
    } else if (ep) {
        reportLine("Exception Type: 0x%08lX  (%s)",
                   (unsigned long)exCode, exceptionName(exCode));
        reportLine("Exception Addr: 0x%p", ep->ExceptionRecord->ExceptionAddress);
        if (exCode == EXCEPTION_ACCESS_VIOLATION &&
            ep->ExceptionRecord->NumberParameters >= 2) {
            reportLine("Access Violation: %s at 0x%llx",
                       ep->ExceptionRecord->ExceptionInformation[0] ? "write" : "read",
                       (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
        }
    }

    reportLine("");
    reportLine("Thread 0 Crashed:");

    // ── DbgHelp stack walk ────────────────────────────────────────────────
    HANDLE process = GetCurrentProcess();
    HANDLE thread  = GetCurrentThread();

    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    CONTEXT ctx = {};
    if (ep) {
        ctx = *ep->ContextRecord;
    } else {
        RtlCaptureContext(&ctx);
    }

    STACKFRAME64 sf = {};
    DWORD machine;

#if defined(_M_AMD64) || defined(__x86_64__)
    machine          = IMAGE_FILE_MACHINE_AMD64;
    sf.AddrPC.Offset    = ctx.Rip;
    sf.AddrFrame.Offset = ctx.Rbp;
    sf.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_ARM64) || defined(__aarch64__)
    machine          = IMAGE_FILE_MACHINE_ARM64;
    sf.AddrPC.Offset    = ctx.Pc;
    sf.AddrFrame.Offset = ctx.Fp;
    sf.AddrStack.Offset = ctx.Sp;
#else // x86
    machine          = IMAGE_FILE_MACHINE_I386;
    sf.AddrPC.Offset    = ctx.Eip;
    sf.AddrFrame.Offset = ctx.Ebp;
    sf.AddrStack.Offset = ctx.Esp;
#endif
    sf.AddrPC.Mode = sf.AddrFrame.Mode = sf.AddrStack.Mode = AddrModeFlat;

    union {
        SYMBOL_INFO si;
        char buf[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
    } symBuf;

    IMAGEHLP_LINE64 lineInfo = {};
    lineInfo.SizeOfStruct = sizeof(lineInfo);

    for (int frame = 0; frame < 64; ++frame) {
        if (!StackWalk64(machine, process, thread, &sf, &ctx,
                         nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr))
            break;
        if (!sf.AddrPC.Offset) break;

        // Symbol name
        symBuf.si.SizeOfStruct = sizeof(SYMBOL_INFO);
        symBuf.si.MaxNameLen   = MAX_SYM_NAME;
        DWORD64 symDisp = 0;
        char symName[512] = "??";
        if (SymFromAddr(process, sf.AddrPC.Offset, &symDisp, &symBuf.si)) {
            snprintf(symName, sizeof(symName), "%s + %llu",
                     symBuf.si.Name, (unsigned long long)symDisp);
        }

        // File + line (requires PDB)
        DWORD lineDisp = 0;
        char fileInfo[512] = "";
        if (SymGetLineFromAddr64(process, sf.AddrPC.Offset, &lineDisp, &lineInfo)) {
            // Shorten path to just filename for readability
            const char *fname = lineInfo.FileName;
            const char *slash = strrchr(fname, '\\');
            if (!slash) slash = strrchr(fname, '/');
            if (slash) fname = slash + 1;
            snprintf(fileInfo, sizeof(fileInfo), "  [%s:%lu]", fname, lineInfo.LineNumber);
        }

        reportLine("  #%-2d  0x%016llx  %s%s",
                   frame, (unsigned long long)sf.AddrPC.Offset,
                   symName, fileInfo);
    }

    SymCleanup(process);

    reportLine("");
    reportLine("========================================================================");
    if (logPath[0])
        reportLine("Crash log: %s", logPath);
    reportLine("========================================================================");

    if (logFile) fclose(logFile);
}

static LONG WINAPI sehFilter(EXCEPTION_POINTERS *ep)
{
    // Ignore C++ exceptions thrown by the runtime (handled by terminate handler)
    if (ep && ep->ExceptionRecord->ExceptionCode == 0xE06D7363)
        return EXCEPTION_CONTINUE_SEARCH;

    writeWindowsReport(ep, nullptr);
    return EXCEPTION_EXECUTE_HANDLER; // terminate process
}

static void winTerminateHandler()
{
    std::string msg;
    try {
        if (auto ep = std::current_exception()) {
            try { std::rethrow_exception(ep); }
            catch (const std::exception &e) { msg = e.what(); }
            catch (...)                     { msg = "(non-std::exception)"; }
        }
    } catch (...) {}

    writeWindowsReport(nullptr, msg.empty() ? "(no info)" : msg.c_str());
    std::abort();
}

static void platformInstall()
{
    SetUnhandledExceptionFilter(sehFilter);
    std::set_terminate(winTerminateHandler);
}

#else
// Unsupported platform — no-op
static void platformInstall() {}
#endif

// ══════════════════════════════════════════════════════════════════════════════
//  Public API
// ══════════════════════════════════════════════════════════════════════════════
void CrashHandler::install(const QString &logDir)
{
    // Binary path
    QByteArray bin = QCoreApplication::applicationFilePath().toLocal8Bit();
    strncpy(g_binPath, bin.constData(), sizeof(g_binPath) - 1);

    // Version string
    QString ver = QCoreApplication::applicationVersion();
    if (ver.isEmpty()) ver = "1.1.0";
    strncpy(g_version,
            QString("%1 %2").arg(QCoreApplication::applicationName(), ver)
                             .toLocal8Bit().constData(),
            sizeof(g_version) - 1);

    // Log directory — create it now (can't use Qt in signal handler later)
    QString dir = logDir.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/crashes"
        : logDir;
    QDir().mkpath(dir);
    strncpy(g_logDir, dir.toLocal8Bit().constData(), sizeof(g_logDir) - 1);

    platformInstall();
}
