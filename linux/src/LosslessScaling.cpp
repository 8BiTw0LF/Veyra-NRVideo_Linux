#include "LosslessScaling.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace lsfg {
namespace {

QString findFileCaseInsensitive(const QString& dir, const QString& name) {
    QDir d(dir);
    if (!d.exists()) return {};
    for (const QString& f : d.entryList(QDir::Files))
        if (f.compare(name, Qt::CaseInsensitive) == 0) return d.absoluteFilePath(f);
    return {};
}

QStringList steamLibraries() {
    const QString home = QDir::homePath();   // e.g. /var/home/<user> on Bazzite, /home/<user> elsewhere
    const QStringList roots = {
        home + "/.local/share/Steam",
        home + "/.steam/steam",
        home + "/.steam/root",
        home + "/.steam/debian-installation",
        home + "/.var/app/com.valvesoftware.Steam/.local/share/Steam",   // Flatpak Steam
        home + "/.var/app/com.valvesoftware.Steam/data/Steam",
        home + "/snap/steam/common/.local/share/Steam",                  // Snap Steam
    };
    QStringList libs;
    auto add = [&](const QString& p) {
        const QString c = QFileInfo(p).canonicalFilePath();
        if (!c.isEmpty() && !libs.contains(c)) libs << c;
    };
    static const QRegularExpression pathRe(R"re("path"\s+"([^"]+)")re");
    for (const QString& r : roots) {
        if (!QFileInfo::exists(r + "/steamapps")) continue;
        add(r);
        QFile vdf(r + "/steamapps/libraryfolders.vdf");
        if (!vdf.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QString text = QString::fromUtf8(vdf.readAll());
        for (auto it = pathRe.globalMatch(text); it.hasNext();) add(it.next().captured(1).replace("\\\\", "\\"));
    }
    return libs;
}

QStringList layerDirsImpl() {
    QStringList dataDirs;
    const QString xdgData = qEnvironmentVariable("XDG_DATA_HOME", QDir::homePath() + "/.local/share");
    dataDirs << xdgData;
    for (const QString& d : qEnvironmentVariable("XDG_DATA_DIRS", "/usr/local/share:/usr/share").split(':', Qt::SkipEmptyParts))
        dataDirs << d;
    dataDirs << "/usr/share" << "/usr/local/share" << "/etc" << qEnvironmentVariable("XDG_CONFIG_HOME", QDir::homePath() + "/.config");
    QStringList out;
    for (const QString& d : dataDirs)
        for (const char* kind : {"/vulkan/implicit_layer.d", "/vulkan/explicit_layer.d"}) {
            const QString p = d + kind;
            if (!out.contains(p)) out << p;
        }
    return out;
}

bool cliPresent() {
    if (!QStandardPaths::findExecutable("lsfg-vk-cli").isEmpty()) return true;
    return QFileInfo::exists(QDir::homePath() + "/.local/bin/lsfg-vk-cli");
}

QString tomlString(QString s) {
    s.replace("\\", "\\\\").replace("\"", "\\\"");
    return "\"" + s + "\"";
}

} // namespace

QStringList layerDirs() { return layerDirsImpl(); }

QStringList oldLayerManifests() {
    QStringList out;
    for (const QString& dir : layerDirsImpl()) {
        QDirIterator it(dir, {"*.json"}, QDir::Files);
        while (it.hasNext()) {
            const QString f = it.next();
            QFile j(f);
            if (!j.open(QIODevice::ReadOnly)) continue;
            const QByteArray body = j.read(64 * 1024);
            if (!body.toLower().contains("lsfg") && !QFileInfo(f).fileName().toLower().contains("lsfg")) continue;
            if (body.contains("LSFGVK")) continue;   // lsfg-vk 2
            const QString c = QFileInfo(f).canonicalFilePath();
            if (!out.contains(c)) out << c;
        }
    }
    return out;
}

Detection detect(const QString& overridePath) {
    Detection d;
    QStringList dirs;
    if (!overridePath.isEmpty()) {
        const QFileInfo fi(overridePath);
        dirs << (fi.isDir() ? fi.absoluteFilePath() : fi.absolutePath());
    }
    for (const QString& lib : steamLibraries()) dirs << lib + "/steamapps/common/Lossless Scaling";
    for (const QString& dir : dirs) {
        d.searched << dir;
        const QString vk = findFileCaseInsensitive(dir, "lsfg-vk.dll");
        const QString ls = findFileCaseInsensitive(dir, "Lossless.dll");
        if (vk.isEmpty() && ls.isEmpty()) continue;
        d.installDir = dir;
        d.dllPath = vk;
        d.losslessDll = ls;
        break;
    }
    for (const QString& dir : layerDirsImpl()) {
        QDirIterator it(dir, {"*.json"}, QDir::Files);
        while (it.hasNext()) {
            const QString f = it.next();
            QFile j(f);
            if (!j.open(QIODevice::ReadOnly)) continue;
            const QByteArray body = j.read(64 * 1024);
            if (!body.toLower().contains("lsfg") && !QFileInfo(f).fileName().toLower().contains("lsfg")) continue;
            d.layerManifest = f;
            d.layerV2 = body.contains("LSFGVK") || cliPresent();
            break;
        }
        if (!d.layerManifest.isEmpty()) break;
    }
    return d;
}

QString configPath() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/VeyAmux/lsfg-vk.toml";
}

QString logPath() { return QFileInfo(configPath()).absolutePath() + "/lsfg-vk.log"; }

QString logSummary(bool* loaded) {
    if (loaded) *loaded = false;
    QFile f(logPath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QStringList lines = QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
    if (loaded) *loaded = !lines.isEmpty();
    auto clean = [](QString l) {
        l = l.trimmed();
        l.remove(QRegularExpression(R"(^\(lsfg-vk\)\s*)"));
        return l;
    };
    for (int i = int(lines.size()) - 1; i >= 0; --i)   // the newest error wins
        if (lines[i].contains("[ERROR]", Qt::CaseInsensitive) || lines[i].contains("[WARNING]", Qt::CaseInsensitive))
            return clean(lines[i]);
    for (int i = int(lines.size()) - 1; i >= 0; --i)
        if (lines[i].contains("profile", Qt::CaseInsensitive)) return clean(lines[i]);
    return lines.isEmpty() ? QString() : clean(lines.last());
}

double recommendedFlowScale(int outputHeight) {
    if (outputHeight >= 2000) return 0.5;
    if (outputHeight >= 1400) return 0.75;
    return 1.0;
}

bool writeConfig(const Detection& d, int multiplier, bool performanceMode, double flowScale, QString* error) {
    const QString path = configPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QString text;
    QTextStream ts(&text);
    ts << "# Written by VeyAmux for its own window only (LSFGVK_CONFIG). Your own lsfg-vk config is not touched.\n"
       << "version = 2\n\n"
       << "[global]\n"
       << "dll = " << tomlString(d.dllPath) << "\n"
       << "allow_fp16 = true\n"
       // lsfg-vk's own log, read back by VeyAmux to show in the menu whether the layer loaded and what it says
       << "log_level = \"debug\"\n"
       << "log_file = " << tomlString(logPath()) << "\n\n"
       << "[[profile]]\n"
       << "name = " << tomlString(kProfileName) << "\n"
       << "active_in = [\"veyamux\"]\n"
       << "multiplier = " << std::clamp(multiplier, 2, 8) << "\n"   // lsfg-vk 2 refuses 1
       << "flow_scale = " << QString::number(std::clamp(flowScale, 0.25, 1.0), 'f', 2) << "\n"
       << "performance_mode = " << (performanceMode ? "true" : "false") << "\n";
    // Rewrite in place (not via rename) so lsfg-vk's file watcher keeps seeing the same file and hot-reloads it.
    QFile f(path);
    if (f.exists()) {
        if (f.open(QIODevice::ReadOnly) && f.readAll() == text.toUtf8()) return true;
        f.close();
    }
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    f.write(text.toUtf8());
    return true;
}

void configureEnvironment(bool enable) {
    if (enable) {
        qputenv("LSFGVK_CONFIG", configPath().toUtf8());
        qputenv("LSFGVK_PROFILE", kProfileName);
        qunsetenv("DISABLE_LSFGVK");
    } else {
        // Keep an installed lsfg-vk (and any user profile that might match "veyamux") off VeyAmux's window.
        qputenv("DISABLE_LSFGVK", "1");
    }
}

} // namespace lsfg
