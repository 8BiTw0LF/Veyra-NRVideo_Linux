// VeyAmux for Linux: capture-card watcher (see Watcher.h).
#include "Watcher.h"
#include "Sandbox.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>

namespace veyamux::watch {

namespace {
QString autostartFile() {
    const QString cfg = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    return cfg + "/autostart/veyamux-watch.desktop";
}
QString pidFile() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/veyamux-watch.pid";
}
// Another VeyAmux window running? (the watcher itself and the UI probe are not counted)
bool veyamuxRunning() {
    const qint64 me = qint64(getpid());
    for (const QString& pid : QDir("/proc").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        bool num = false;
        const qint64 p = pid.toLongLong(&num);
        if (!num || p == me) continue;
        if (QFileInfo("/proc/" + pid + "/exe").symLinkTarget().section('/', -1) != "veyamux") continue;
        QFile c("/proc/" + pid + "/cmdline");
        if (!c.open(QIODevice::ReadOnly)) continue;
        const QByteArray cmd = c.readAll();
        if (cmd.contains("--watch") || cmd.contains("--probe-ui")) continue;
        return true;
    }
    return false;
}
// Only the capture node of a card (index 0), not its metadata node
bool isCaptureNode(const QString& dev) {
    QFile f("/sys/class/video4linux/" + QFileInfo(dev).fileName() + "/index");
    return !f.open(QIODevice::ReadOnly) || f.readAll().trimmed() == "0";
}
} // namespace

QString cardName(const QString& devicePath) {
    QFile f("/sys/class/video4linux/" + QFileInfo(QFileInfo(devicePath).canonicalFilePath()).fileName() + "/name");
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
}

QString findDeviceByName(const QString& name) {
    if (name.isEmpty()) return {};
    QStringList devs = QDir("/dev").entryList({"video*"}, QDir::System);
    std::sort(devs.begin(), devs.end(), [](const QString& a, const QString& b) { return a.mid(5).toInt() < b.mid(5).toInt(); });
    for (const QString& d : devs) {
        const QString p = "/dev/" + d;
        if (isCaptureNode(p) && cardName(p) == name) return p;
    }
    return {};
}

int run(const QString& self) {
    {   // single instance
        QFile pf(pidFile());
        if (pf.open(QIODevice::ReadOnly)) {
            const qint64 old = pf.readAll().trimmed().toLongLong();
            if (old > 0 && old != getpid() && QFileInfo::exists("/proc/" + QString::number(old)) &&
                QFile("/proc/" + QString::number(old) + "/cmdline").open(QIODevice::ReadOnly)) {
                QFile c("/proc/" + QString::number(old) + "/cmdline");
                if (c.open(QIODevice::ReadOnly) && c.readAll().contains("--watch")) { std::fprintf(stderr, "VeyAmux watcher already running\n"); return 0; }
            }
        }
        QDir().mkpath(QFileInfo(pidFile()).absolutePath());
        QSaveFile out(pidFile());
        if (out.open(QIODevice::WriteOnly)) { out.write(QByteArray::number(qint64(getpid()))); out.commit(); }
    }
    const int fd = inotify_init1(IN_CLOEXEC);
    if (fd < 0 || inotify_add_watch(fd, "/dev", IN_CREATE | IN_ATTRIB) < 0) { std::perror("inotify"); return 1; }
    std::fprintf(stderr, "VeyAmux watcher: waiting for the capture card\n");
    auto lastStart = std::chrono::steady_clock::now() - std::chrono::minutes(1);
    alignas(inotify_event) char buf[4096];
    for (;;) {
        const ssize_t n = read(fd, buf, sizeof buf);
        if (n <= 0) { std::this_thread::sleep_for(std::chrono::seconds(1)); continue; }
        bool video = false;
        for (char* p = buf; p < buf + n;) {
            auto* ev = reinterpret_cast<inotify_event*>(p);
            if (ev->len && QByteArray(ev->name).startsWith("video")) video = true;
            p += sizeof(inotify_event) + ev->len;
        }
        if (!video) continue;
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));   // let udev finish (permissions, all nodes)
        if (std::chrono::steady_clock::now() - lastStart < std::chrono::seconds(10)) continue;
        QSettings q;
        if (!q.value("watch/enabled", false).toBool()) continue;
        const QString dev = findDeviceByName(q.value("cap/name").toString());
        if (dev.isEmpty() || veyamuxRunning()) continue;
        std::fprintf(stderr, "VeyAmux watcher: %s appeared, starting VeyAmux\n", qPrintable(dev));
        QProcess::startDetached(self, {"--fullscreen", "--capture", dev});
        lastStart = std::chrono::steady_clock::now();
    }
}

bool autostartEnabled() { return QSettings().value("watch/enabled", false).toBool() && QFileInfo::exists(autostartFile()); }

bool setAutostart(bool on, QString& error) {
    QSettings().setValue("watch/enabled", on);
    // stop a running watcher (it is restarted below when turning on, with the current AppImage path)
    QFile pf(pidFile());
    if (pf.open(QIODevice::ReadOnly)) {
        const qint64 old = pf.readAll().trimmed().toLongLong();
        QFile c("/proc/" + QString::number(old) + "/cmdline");
        if (old > 0 && c.open(QIODevice::ReadOnly) && c.readAll().contains("--watch")) ::kill(pid_t(old), SIGTERM);
    }
    if (!on) {
        QFile::remove(autostartFile());
        return true;
    }
    QDir().mkpath(QFileInfo(autostartFile()).absolutePath());
    QSaveFile f(autostartFile());
    if (!f.open(QIODevice::WriteOnly)) { error = "Cannot write " + autostartFile(); return false; }
    // started by the desktop at login: the AppImage file or the binary
    const sandbox::Launcher run = sandbox::launcher();
    QString exec;
    for (const QString& a : QStringList{run.program} + run.args + QStringList{"--watch"})
        exec += (exec.isEmpty() ? "" : " ") + ("\"" + QString(a).replace("\"", "\\\"") + "\"");
    f.write(("[Desktop Entry]\nType=Application\nName=VeyAmux capture-card watcher\n"
             "Comment=Starts VeyAmux when the capture card is plugged in\nExec=" + exec + "\n"
             "Icon=veyamux\nTerminal=false\nNoDisplay=true\nX-GNOME-Autostart-enabled=true\nX-KDE-autostart-phase=2\n").toUtf8());
    if (!f.commit()) { error = "Cannot write " + autostartFile(); return false; }
    // and now, with the same command: an AppImage's inner binary disappears when the AppImage that mounted it exits
    QProcess::startDetached(run.program, run.args + QStringList{"--watch"});
    return true;
}

} // namespace veyamux::watch
