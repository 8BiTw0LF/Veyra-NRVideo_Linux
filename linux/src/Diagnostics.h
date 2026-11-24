// VeyAmux for Linux: what a bug report needs, gathered by the app itself.
//
// Both builds keep the last log lines in memory (for the report). VeyAmux Debug (VEYAMUX_DEBUG_BUILD) also writes the
// log to a file, routes FFmpeg's warnings into it and leaves a backtrace behind when it crashes; the next report
// includes it. The report itself is put together in PlayerBridgeDebug.cpp, which knows the app's state.
#pragma once
#include <QString>
#include <QStringList>

namespace veyamux::diag {

constexpr bool kDebugBuild =
#ifdef VEYAMUX_DEBUG_BUILD
    true;
#else
    false;
#endif

// "1.8" or, in VeyAmux Debug, "1.8.<build>"; and the commit it was built from
QString buildVersion(const QString& base);
QString gitSha();

// Installs the Qt message handler (ring buffer, plus the log file in VeyAmux Debug) and, in VeyAmux Debug, FFmpeg's
// log callback and the crash handler. Call once, early in main().
void install(bool files = true);   // files: the log file and crash backtraces (not for helper processes)
// The newest `lines` log lines, oldest first.
QStringList recentLog(int lines);
// VeyAmux Debug: the current log file and the folder with logs and crash backtraces.
QString logFile();
QString debugDir();
// Crash backtraces left by earlier runs (newest first), and their content.
QStringList crashFiles();
QString readText(const QString& path, int maxBytes = 64 * 1024);

// The machine: distribution, kernel, CPU, memory, desktop and session, sandboxes, relevant environment.
QString systemInfo();
// Output of a command line tool if it is installed (vainfo, pactl, vulkaninfo ...), at most `timeoutMs`.
QString tool(const QString& program, const QStringList& args, int timeoutMs = 4000, int maxLines = 80);

// Values of settings keys whose names suggest a secret are replaced by "(hidden)".
bool secretKey(const QString& key);

} // namespace veyamux::diag
