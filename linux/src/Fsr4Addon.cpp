#include "Fsr4Addon.h"
#include "Download.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

namespace fsr4addon {
namespace {

constexpr const char* kUrl = "https://github.com/8BiTw0LF/VeyAmux-FSR4/releases/download/v1/veyamux-fsr4-1.tar.gz";
// the add-on's release v1 (VeyAmux-FSR4, built reproducibly by its CI; the same as a local build.sh 1)
constexpr const char* kSha256 = "f4b957e0f5cd83f399ab50795e6188c1763a56e6036de77e3a9f7be85b1002ed";
constexpr qint64 kMaxBytes = 64ll * 1024 * 1024;

QString baseDir() { return QDir::homePath() + "/.local/share/VeyAmux/fsr4"; }

// every file the manifest lists, with the right SHA-256; full = hash them (install), else only check they exist
bool matchesManifest(const QString& dir, bool full, QString* error = nullptr) {
    QFile mf(dir + "/manifest.json");
    if (!mf.open(QIODevice::ReadOnly)) { if (error) *error = "manifest.json is missing"; return false; }
    const QJsonObject m = QJsonDocument::fromJson(mf.readAll()).object();
    const QJsonObject files = m.value("files").toObject();
    if (m.value("name").toString() != "veyamux-fsr4" || files.isEmpty()) { if (error) *error = "not a VeyAmux FSR 4 add-on"; return false; }
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QString f = it.key();
        if (f.contains('/') || f.startsWith('.')) { if (error) *error = "unexpected file " + f; return false; }
        const QString p = dir + "/" + f;
        if (!QFileInfo::exists(p)) { if (error) *error = f + " is missing"; return false; }
        if (full && veyamux::fileSha256(p) != it.value().toString()) { if (error) *error = f + " does not match its checksum"; return false; }
    }
    return true;
}

} // namespace

QString installedDir() {
    const QString env = qEnvironmentVariable("VEYAMUX_FSR4_DIR");
    if (!env.isEmpty()) return QFileInfo::exists(env + "/manifest.json") ? env : QString();
    const QString dir = baseDir() + "/" + kVersion;
    return matchesManifest(dir, false) ? dir : QString();
}

bool available() { return !QLatin1String(kSha256).isEmpty(); }

InstallResult install(const std::function<void(int)>& progress) {
    InstallResult r;
    if (QLatin1String(kSha256).isEmpty()) { r.message = "The FSR 4 add-on is not published yet"; return r; }
    QTemporaryDir tmp;
    if (!tmp.isValid()) { r.message = "Cannot create a temporary folder"; return r; }
    const QString archive = tmp.filePath("veyamux-fsr4.tar.gz");
    QString error;
    if (!veyamux::httpsDownload(QString::fromLatin1(kUrl), archive, kMaxBytes, progress, error)) {
        r.message = "Download failed (" + QString(kUrl) + "): " + error;
        return r;
    }
    if (veyamux::fileSha256(archive) != QLatin1String(kSha256)) {
        r.message = "The downloaded file does not match the expected checksum, so it was not installed";
        return r;
    }
    // only plain files under fsr4/
    QProcess list;
    list.start("tar", {"-tzf", archive});
    if (!list.waitForFinished(60000) || list.exitCode() != 0) { r.message = "tar could not read the archive"; return r; }
    for (QString e : QString::fromUtf8(list.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts)) {
        while (e.startsWith("./")) e = e.mid(2);
        const QStringList parts = e.split('/', Qt::SkipEmptyParts);
        if (e.startsWith('/') || parts.contains("..") || parts.isEmpty() || parts.first() != "fsr4" || parts.size() > 2) {
            r.message = "unexpected file in the archive: " + e;
            return r;
        }
    }
    const QString unpack = tmp.filePath("x");
    QDir().mkpath(unpack);
    QProcess tar;
    tar.start("tar", {"-xzf", archive, "-C", unpack, "--no-same-owner"});
    if (!tar.waitForFinished(120000) || tar.exitCode() != 0) { r.message = "Unpacking failed: " + QString::fromUtf8(tar.readAllStandardError()); return r; }
    if (!matchesManifest(unpack + "/fsr4", true, &error)) { r.message = "The add-on is not complete: " + error; return r; }
    const QString target = baseDir() + "/" + kVersion;
    QDir(target).removeRecursively();
    QDir().mkpath(baseDir());
    if (!QDir().rename(unpack + "/fsr4", target)) {
        // across file systems: copy
        QDir().mkpath(target);
        for (const QString& f : QDir(unpack + "/fsr4").entryList(QDir::Files))
            if (!QFile::copy(unpack + "/fsr4/" + f, target + "/" + f)) { r.message = "Cannot write " + target; return r; }
    }
    r.ok = true;
    r.message = "FSR 4 was installed into " + target;
    return r;
}

} // namespace fsr4addon
