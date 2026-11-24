// VeyAmux for Linux: what a bug report needs (see Diagnostics.h).
#include "Diagnostics.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QThread>

#include <deque>
#include <mutex>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include <libavutil/log.h>
}

namespace veyamux::diag {
namespace {

std::mutex logM;
std::deque<QString> ring;
constexpr size_t kRing = 3000;
FILE* logFp = nullptr;
QString logPath, dirPath;
QtMessageHandler previous = nullptr;

// crash handler state, prepared before any signal can arrive
char crashPath[1024] = {};
char crashHead[512] = {};

void addLine(const QString& line) {
    std::lock_guard l(logM);
    ring.push_back(line);
    while (ring.size() > kRing) ring.pop_front();
    if (logFp) {
        const QByteArray b = line.toUtf8();
        std::fwrite(b.constData(), 1, size_t(b.size()), logFp);
        std::fputc('\n', logFp);
        std::fflush(logFp);   // the last lines before a crash must be on disk
    }
}

void onMessage(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    static const char* tags[] = {"D", "W", "C", "F", "I"};
    const int t = int(type);
    addLine(QStringLiteral("%1 [%2] %3")
                .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"))
                .arg(t >= 0 && t < 5 ? tags[t] : "?")
                .arg(msg));
    if (previous) previous(type, ctx, msg);
}

void onFfmpeg(void* avcl, int level, const char* fmt, va_list vl) {
    if (level > AV_LOG_WARNING) return;
    char line[1024];
    int prefix = 1;
    av_log_format_line2(avcl, level, fmt, vl, line, sizeof line, &prefix);
    QString s = QString::fromUtf8(line).trimmed();
    if (s.isEmpty()) return;
    addLine(QStringLiteral("%1 [FFmpeg] %2").arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"), s));
}

void writeAll(int fd, const char* s) {
    size_t n = std::strlen(s);
    while (n) {
        const ssize_t w = ::write(fd, s, n);
        if (w <= 0) return;
        s += w; n -= size_t(w);
    }
}

// Only async-signal-safe calls in here (open, write, backtrace after it was warmed up, raise).
void onCrash(int sig) {
    const int fd = ::open(crashPath, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd >= 0) {
        writeAll(fd, crashHead);
        writeAll(fd, "signal: ");
        const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGBUS ? "SIGBUS"
                         : sig == SIGFPE ? "SIGFPE" : sig == SIGILL ? "SIGILL" : "?";
        writeAll(fd, name);
        writeAll(fd, "\nbacktrace:\n");
        void* frames[96];
        const int n = backtrace(frames, 96);
        backtrace_symbols_fd(frames, n, fd);
        ::close(fd);
    }
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

} // namespace

QString gitSha() { return QStringLiteral(VEYAMUX_GIT_SHA); }

QString buildVersion(const QString& base) {
    return kDebugBuild ? base + "." + QString::number(VEYAMUX_BUILD_NUMBER) : base;
}

QString debugDir() { return dirPath; }
QString logFile() { return logPath; }

void install(bool files) {
    if (kDebugBuild && files) {
        dirPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/debug";
        QDir().mkpath(dirPath);
        // keep the ten newest logs
        QDir d(dirPath);
        const QStringList old = d.entryList({"veyamux-*.log"}, QDir::Files, QDir::Time);
        for (int i = 9; i < old.size(); ++i) d.remove(old[i]);
        const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
        logPath = dirPath + "/veyamux-" + stamp + ".log";
        logFp = std::fopen(QFile::encodeName(logPath).constData(), "w");
        av_log_set_callback(onFfmpeg);

        const QByteArray cp = QFile::encodeName(dirPath + "/crash-" + stamp + ".txt");
        std::snprintf(crashPath, sizeof crashPath, "%s", cp.constData());
        std::snprintf(crashHead, sizeof crashHead, "VeyAmux Debug %s (%s) crashed\nlog: %s\n",
                      qPrintable(QCoreApplication::applicationVersion()), VEYAMUX_GIT_SHA,
                      QFile::encodeName(logPath).constData());
        void* warm[2];
        backtrace(warm, 2);   // loads libgcc now, not inside the signal handler
        for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) std::signal(sig, onCrash);
    }
    previous = qInstallMessageHandler(onMessage);
    qInfo("VeyAmux %s (%s)%s", qPrintable(QCoreApplication::applicationVersion()), VEYAMUX_GIT_SHA,
          kDebugBuild ? " Debug" : "");
}

QStringList recentLog(int lines) {
    std::lock_guard l(logM);
    QStringList out;
    const size_t n = std::min(ring.size(), size_t(std::max(lines, 0)));
    for (size_t i = ring.size() - n; i < ring.size(); ++i) out << ring[i];
    return out;
}

QStringList crashFiles() {
    if (dirPath.isEmpty()) return {};
    QStringList out;
    QDir d(dirPath);
    for (const QString& f : d.entryList({"crash-*.txt"}, QDir::Files, QDir::Time)) out << d.filePath(f);
    return out;
}

QString readText(const QString& path, int maxBytes) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray b = f.read(maxBytes);
    QString s = QString::fromUtf8(b);
    if (f.size() > maxBytes) s += "\n… (cut)";
    return s;
}

QString tool(const QString& program, const QStringList& args, int timeoutMs, int maxLines) {
    if (QStandardPaths::findExecutable(program).isEmpty()) return QStringLiteral("(%1 is not installed)").arg(program);
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    // the AppImage's own libraries must not leak into the system tool
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove("LD_LIBRARY_PATH");
    env.remove("QT_PLUGIN_PATH");
    p.setProcessEnvironment(env);
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs)) { p.kill(); p.waitForFinished(500); return QStringLiteral("(%1 did not answer)").arg(program); }
    QStringList lines = QString::fromUtf8(p.readAll()).split('\n');
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();
    if (lines.size() > maxLines) { lines = lines.mid(0, maxLines); lines << "…"; }
    return lines.join('\n');
}

bool secretKey(const QString& key) {
    const QString k = key.toLower();
    for (const char* s : {"key", "token", "password", "secret", "account", "regist", "psn", "morning", "credential",
                          "pin", "cookie", "auth", "mac", "url"})
        if (k.contains(QLatin1String(s))) return true;
    return false;
}

QString systemInfo() {
    QStringList r;
    QString os = QSysInfo::prettyProductName();
    {
        QFile f("/etc/os-release");
        if (f.open(QIODevice::ReadOnly)) {
            QString variant, image;
            for (const QString& line : QString::fromUtf8(f.readAll()).split('\n')) {
                if (line.startsWith("VARIANT=")) variant = line.mid(8).remove('"');
                if (line.startsWith("IMAGE_NAME=") || line.startsWith("BUILD_ID=")) image += " " + line.section('=', 1).remove('"');
            }
            if (!variant.isEmpty()) os += " · " + variant;
            if (!image.trimmed().isEmpty()) os += " ·" + image;
        }
    }
    r << "OS: " + os;
    r << "Kernel: " + QSysInfo::kernelVersion() + " (" + QSysInfo::currentCpuArchitecture() + ")";
    {
        QFile f("/proc/cpuinfo");
        QString model;
        int threads = 0;
        if (f.open(QIODevice::ReadOnly))
            for (const QString& line : QString::fromUtf8(f.readAll()).split('\n')) {
                if (line.startsWith("model name") && model.isEmpty()) model = line.section(':', 1).trimmed();
                if (line.startsWith("processor")) ++threads;
            }
        r << QStringLiteral("CPU: %1 (%2 threads)").arg(model.isEmpty() ? "?" : model).arg(threads);
    }
    {
        QFile f("/proc/meminfo");
        if (f.open(QIODevice::ReadOnly)) {
            const QString all = QString::fromUtf8(f.readAll());
            auto kb = [&](const char* k) {
                for (const QString& line : all.split('\n'))
                    if (line.startsWith(k)) return line.section(':', 1).trimmed().section(' ', 0, 0).toLongLong();
                return 0LL;
            };
            r << QStringLiteral("Memory: %1 GB, %2 GB free").arg(kb("MemTotal") / 1048576.0, 0, 'f', 1).arg(kb("MemAvailable") / 1048576.0, 0, 'f', 1);
        }
    }
    r << "Desktop: " + qEnvironmentVariable("XDG_CURRENT_DESKTOP", "?") + " · session " + qEnvironmentVariable("XDG_SESSION_TYPE", "?") +
             " · Qt platform " + QGuiApplication::platformName() + " · Qt " + qVersion();
    QStringList where;
    if (!qEnvironmentVariable("GAMESCOPE_WAYLAND_DISPLAY").isEmpty()) where << "gamescope";
    if (qEnvironmentVariable("SteamDeck") == "1") where << "Steam Deck";
    if (!qEnvironmentVariable("SteamAppId").isEmpty() || !qEnvironmentVariable("SteamGameId").isEmpty()) where << "started by Steam";
    if (QFileInfo::exists("/.flatpak-info")) where << "Flatpak";
    if (QFileInfo::exists("/run/.containerenv")) where << "container (distrobox/toolbox)";
    if (!qEnvironmentVariable("APPIMAGE").isEmpty()) where << "AppImage " + qEnvironmentVariable("APPIMAGE");
    r << "Runs: " + (where.isEmpty() ? QStringLiteral("directly") : where.join(", "));
    QStringList env;
    for (const QString& kv : QProcess::systemEnvironment()) {
        const QString k = kv.section('=', 0, 0);
        if (k.startsWith("VEYAMUX_") || k.startsWith("QSG_") || k.startsWith("RADV_") || k.startsWith("MESA_") ||
            k.startsWith("VK_") || k.startsWith("ENABLE_") || k.startsWith("LSFG_") || k == "QT_QPA_PLATFORM" ||
            k == "QT_SCALE_FACTOR" || k == "QT_VULKAN_LIB" || k == "DXVK_HDR" || k == "PROTON_ENABLE_HDR")
            env << (secretKey(k) ? k + "=(hidden)" : kv);
    }
    env.sort();
    r << "Environment: " + (env.isEmpty() ? QStringLiteral("(nothing special)") : env.join("  "));
    return r.join('\n');
}

} // namespace veyamux::diag
