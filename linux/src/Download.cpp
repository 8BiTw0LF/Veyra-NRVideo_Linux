#include "Download.h"

#include <QCryptographicHash>
#include <QFile>

#include <curl/curl.h>
#include <unistd.h>

namespace veyamux {
namespace {

struct Sink {
    QFile* file = nullptr;
    qint64 bytes = 0, max = 0;
    const std::function<void(int)>* progress = nullptr;
    int lastPercent = -1;
};

size_t onData(char* ptr, size_t size, size_t n, void* user) {
    auto* d = static_cast<Sink*>(user);
    const qint64 len = qint64(size * n);
    d->bytes += len;
    if (d->bytes > d->max) return 0;
    return d->file->write(ptr, len) == len ? size_t(len) : 0;
}

int onProgress(void* user, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
    auto* d = static_cast<Sink*>(user);
    if (total > 0 && d->progress && *d->progress) {
        const int pc = int(now * 100 / total);
        if (pc != d->lastPercent) { d->lastPercent = pc; (*d->progress)(pc); }
    }
    return 0;
}

} // namespace

bool httpsDownload(const QString& url, const QString& path, qint64 maxBytes, const std::function<void(int)>& progress, QString& error) {
    if (!url.startsWith("https://")) { error = "only HTTPS downloads"; return false; }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { error = "Cannot write " + path; return false; }
    CURL* c = curl_easy_init();
    if (!c) { error = "curl could not start"; return false; }
    Sink d{&f, 0, maxBytes, &progress, -1};
    char err[CURL_ERROR_SIZE] = {};
    const QByteArray u = url.toUtf8();
    curl_easy_setopt(c, CURLOPT_URL, u.constData());
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
        error = d.bytes > maxBytes ? QString("the download is larger than expected") : QString::fromUtf8(err[0] ? err : curl_easy_strerror(rc));
        return false;
    }
    return true;
}

QString fileSha256(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

} // namespace veyamux
