// VeyAmux for Linux: lsfg-vk 2 install (see LsfgInstall.h).
#include "LsfgInstall.h"
#include "LosslessScaling.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QStandardPaths>
#include <QProcess>
#include <QSet>
#include <QTemporaryDir>

#include <curl/curl.h>
#include <unistd.h>

#include <algorithm>

namespace lsfg {
namespace {

// The official build and its checksum. A new lsfg-vk release means a new VeyAmux release with the new pair.
constexpr const char* kUrl = "https://builds.lsfg-vk.dev/lsfg-vk-2.0.0.tar.xz";
constexpr const char* kSha256 = "08bdbdf373a111022df87dac7aa87e3b564bb841f961552e3ca85fea12b5aa74";
constexpr qint64 kMaxBytes = 64ll * 1024 * 1024;

struct Download {
    QFile* file = nullptr;
    qint64 bytes = 0;
    const std::function<void(int)>* progress = nullptr;
    int lastPercent = -1;
};

size_t onData(char* ptr, size_t size, size_t n, void* user) {
    auto* d = static_cast<Download*>(user);
    const qint64 len = qint64(size * n);
    d->bytes += len;
    if (d->bytes > kMaxBytes) return 0;
    return d->file->write(ptr, len) == len ? size_t(len) : 0;
}

int onProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto* d = static_cast<Download*>(user);
    if (total > 0 && d->progress && *d->progress) {
        const int pc = int(now * 100 / total);
        if (pc != d->lastPercent) { d->lastPercent = pc; (*d->progress)(pc); }
    }
    return 0;
}

bool download(const QString& path, const std::function<void(int)>& progress, QString& error) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { error = "Cannot write " + path; return false; }
    CURL* c = curl_easy_init();
    if (!c) { error = "curl could not start"; return false; }
    Download d{&f, 0, &progress, -1};
    char err[CURL_ERROR_SIZE] = {};
    curl_easy_setopt(c, CURLOPT_URL, kUrl);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#endif
    curl_easy_setopt(c, CURLOPT_USERAGENT, "VeyAmux");
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onData);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &d);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &d);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, err);
    // the AppImage's libcurl looks for the CA file where the build system keeps it; use the system's
    for (const char* ca : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                           "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem"})
        if (access(ca, R_OK) == 0) { curl_easy_setopt(c, CURLOPT_CAINFO, ca); break; }
    const CURLcode rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    f.close();
    if (rc != CURLE_OK) {
        error = d.bytes > kMaxBytes ? QString("the download is larger than expected")
                                    : QString::fromUtf8(err[0] ? err : curl_easy_strerror(rc));
        return false;
    }
    return true;
}

QString sha256(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

// Every entry must land under bin/, lib/ or share/ of the prefix.
bool safeEntries(const QString& archive, QString& error) {
    QProcess tar;
    tar.start("tar", {"-tJf", archive});
    if (!tar.waitForFinished(60000) || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        error = "tar could not read the archive: " + QString::fromUtf8(tar.readAllStandardError()).trimmed();
        if (error.endsWith(": ")) error += "is tar (and xz) installed?";
        return false;
    }
    const QStringList lines = QString::fromUtf8(tar.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    if (lines.isEmpty()) { error = "the archive is empty"; return false; }
    for (QString e : lines) {
        while (e.startsWith("./")) e = e.mid(2);
        if (e.isEmpty() || e == ".") continue;
        const QStringList parts = e.split('/', Qt::SkipEmptyParts);
        if (e.startsWith('/') || parts.contains("..") || !QStringList{"bin", "lib", "share"}.contains(parts.first())) {
            error = "unexpected file in the archive: " + e;
            return false;
        }
    }
    return true;
}

QString libraryOf(const QString& manifest) {
    QFile f(manifest);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject layer = QJsonDocument::fromJson(f.readAll()).object().value("layer").toObject();
    const QString lib = layer.value("library_path").toString();
    if (lib.isEmpty()) return {};
    if (lib.startsWith('/')) return lib;
    if (lib.contains('/')) return QFileInfo(QFileInfo(manifest).absolutePath() + "/" + lib).absoluteFilePath();
    return {};   // a bare file name is looked up by the loader in the library path: not ours to move
}

bool inHome(const QString& path) {
    const QString home = QFileInfo(QDir::homePath()).canonicalFilePath();
    const QString c = QFileInfo(path).canonicalFilePath();
    return !c.isEmpty() && (c.startsWith(home + "/"));
}

bool isV2Manifest(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) && f.read(64 * 1024).contains("LSFGVK");
}

} // namespace

QString systemOldLayer() {
    for (const QString& m : oldLayerManifests())
        if (!inHome(m)) return m;
    return {};
}

InstallResult installV2(const std::function<void(int)>& progress) {
    InstallResult r;
    // one install at a time, also across VeyAmux windows
    const QString lockDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/VeyAmux";
    QDir().mkpath(lockDir);
    QLockFile lock(lockDir + "/lsfg-vk-install.lock");
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { r.message = "another VeyAmux window is installing lsfg-vk 2 right now"; return r; }
    if (const QString sys = systemOldLayer(); !sys.isEmpty()) {
        r.message = "lsfg-vk 1 is installed for the whole system (" + sys + "). Remove it with your package manager, "
                    "then try again";
        return r;
    }
    // lsfg-vk 1 in the home folder: manifests and the libraries they load, noted before anything changes
    struct Old { QString manifest, library; };
    QList<Old> old;
    for (const QString& m : oldLayerManifests()) old.append({m, libraryOf(m)});
    const QString known = QDir::homePath() + "/.local/lib/liblsfg-vk.so";   // lsfg-vk 1's library in ~/.local
    if (QFileInfo::exists(known) && std::none_of(old.begin(), old.end(), [&](const Old& o) { return o.library == known; }))
        old.append({QString(), known});

    QTemporaryDir tmp;
    if (!tmp.isValid()) { r.message = "Cannot create a temporary folder"; return r; }
    const QString archive = tmp.filePath(QString("lsfg-vk-%1.tar.xz").arg(kV2Version));
    QString error;
    if (!download(archive, progress, error)) { r.message = "Download failed (" + QString(kUrl) + "): " + error; return r; }
    if (sha256(archive) != QLatin1String(kSha256)) {
        r.message = "The downloaded file does not match the expected checksum, so it was not installed";
        return r;
    }
    if (!safeEntries(archive, error)) { r.message = error; return r; }
    const QString prefix = QDir::homePath() + "/.local";
    QDir().mkpath(prefix);
    QProcess tar;
    tar.start("tar", {"-xJf", archive, "-C", prefix, "--no-same-owner", "--no-overwrite-dir"});
    if (!tar.waitForFinished(120000) || tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        r.message = "Unpacking into " + prefix + " failed: " + QString::fromUtf8(tar.readAllStandardError()).trimmed();
        return r;
    }

    // lsfg-vk 1 aside (moved, not deleted), except files lsfg-vk 2 just replaced or uses itself
    QSet<QString> v2Libs;
    for (const QString& dir : layerDirs())
        for (const QString& f : QDir(dir).entryList({"*.json"}, QDir::Files))
            if (isV2Manifest(dir + "/" + f)) v2Libs.insert(QFileInfo(libraryOf(dir + "/" + f)).canonicalFilePath());
    const QString backup = QDir::homePath() + "/.local/share/VeyAmux/lsfg-vk-1-backup/" +
                           QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    auto moveAside = [&](const QString& path) {
        if (path.isEmpty() || !QFileInfo::exists(path) || !inHome(path)) return;
        if (!QDir().mkpath(backup)) return;
        QString target = backup + "/" + QFileInfo(path).fileName();
        for (int i = 2; QFileInfo::exists(target); ++i) target = backup + "/" + QString::number(i) + "-" + QFileInfo(path).fileName();
        if (QFile::rename(path, target)) r.backupDir = backup;
    };
    for (const Old& o : old) {
        if (!o.manifest.isEmpty() && !isV2Manifest(o.manifest)) moveAside(o.manifest);
        if (!o.library.isEmpty() && !v2Libs.contains(QFileInfo(o.library).canonicalFilePath())) moveAside(o.library);
    }

    const Detection d = detect();
    if (d.layerManifest.isEmpty() || !isV2Manifest(d.layerManifest)) {
        r.message = "lsfg-vk 2 was unpacked into " + prefix + ", but its Vulkan layer was not found afterwards";
        return r;
    }
    r.ok = true;
    r.message = "lsfg-vk " + QString(kV2Version) + " was installed into " + prefix;
    return r;
}

} // namespace lsfg
