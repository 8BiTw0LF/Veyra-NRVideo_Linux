#include "ObsStream.h"

#include <QBuffer>
#include <QHostAddress>
#include <QMetaObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <algorithm>
#include <chrono>

namespace {
constexpr qint64 kMaxBacklog = 8 * 1024 * 1024;   // a viewer this far behind skips frames
}

ObsStream::ObsStream(QObject* parent) : QObject(parent) {
    for (int i = 0; i < 2; ++i) encoders_.emplace_back([this] { encoderLoop(); });
}

ObsStream::~ObsStream() {
    stop();
    { std::lock_guard l(m_); quit_ = true; }
    cv_.notify_all();
    for (auto& t : encoders_) t.join();
}

bool ObsStream::start(quint16 port, QString* error) {
    if (server_ && server_->isListening() && port_ == port) return true;
    stop();
    server_ = new QTcpServer(this);
    if (!server_->listen(QHostAddress::LocalHost, port)) {
        if (error) *error = server_->errorString();
        delete server_;
        server_ = nullptr;
        return false;
    }
    port_ = server_->serverPort();
    connect(server_, &QTcpServer::newConnection, this, &ObsStream::onNewConnection);
    return true;
}

void ObsStream::stop() {
    for (auto& s : streams_) if (s) s->abort();
    streams_.clear();
    if (server_) { server_->close(); server_->deleteLater(); server_ = nullptr; }
    if (viewers_.exchange(0) != 0) emit viewersChanged();
    std::lock_guard l(m_);
    jobs_.clear();
}

bool ObsStream::running() const { return server_ && server_->isListening(); }

QSize ObsStream::targetSize(int w, int h) const {
    const int mh = maxHeight_.load();
    if (w <= 0 || h <= 0) return {};
    if (mh <= 0 || h <= mh) return QSize(w & ~1, h & ~1);
    const int th = mh;
    const int tw = int(std::lround(double(w) * th / h));
    return QSize(std::max(2, tw & ~1), th & ~1);
}

void ObsStream::pushFrame(QImage img, int delayMs) {
    if (!wantsFrames() || img.isNull()) return;
    std::lock_guard l(m_);
    // keep at most a couple of frames in flight: newest wins
    while (jobs_.size() >= 3) jobs_.pop_front();
    jobs_.push_back({std::move(img), nextSeq_++, std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, delayMs))});
    cv_.notify_one();
}

void ObsStream::encoderLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock l(m_);
            cv_.wait(l, [this] { return quit_ || !jobs_.empty(); });
            if (quit_) return;
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        std::this_thread::sleep_until(job.due);
        QImage img = std::move(job.img);
        const QSize t = targetSize(img.width(), img.height());
        if (t.isValid() && t != img.size()) img = img.scaled(t, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (img.format() != QImage::Format_RGB888 && img.format() != QImage::Format_RGB32) img = img.convertToFormat(QImage::Format_RGB888);
        QByteArray jpeg;
        QBuffer buf(&jpeg);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "JPG", 85);
        QMetaObject::invokeMethod(this, [this, seq = job.seq, jpeg = std::move(jpeg)]() mutable { broadcast(seq, std::move(jpeg)); },
                                  Qt::QueuedConnection);
    }
}

void ObsStream::broadcast(quint64 seq, QByteArray jpeg) {
    if (seq <= lastSent_) return;   // the other encoder already sent a newer frame
    lastSent_ = seq;
    lastJpeg_ = jpeg;
    const QByteArray head = "--veyamuxframe\r\nContent-Type: image/jpeg\r\nContent-Length: " + QByteArray::number(jpeg.size()) + "\r\n\r\n";
    for (auto& s : streams_) {
        if (!s || s->state() != QAbstractSocket::ConnectedState) continue;
        if (s->bytesToWrite() > kMaxBacklog) continue;
        s->write(head);
        s->write(jpeg);
        s->write("\r\n");
    }
}

void ObsStream::onNewConnection() {
    while (QTcpSocket* s = server_->nextPendingConnection()) {
        connect(s, &QTcpSocket::readyRead, this, [this, s] { onReadyRead(s); });
        connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
    }
}

void ObsStream::onReadyRead(QTcpSocket* s) {
    if (s->property("served").toBool()) { s->readAll(); return; }
    QByteArray req = s->property("req").toByteArray() + s->readAll();
    if (!req.contains("\r\n\r\n")) {
        if (req.size() > 16384) s->abort();
        else s->setProperty("req", req);
        return;
    }
    s->setProperty("served", true);
    const QList<QByteArray> line = req.left(req.indexOf("\r\n")).split(' ');
    const QByteArray path = line.size() >= 2 ? line[1].split('?').first() : QByteArray("/");
    auto reply = [s](const QByteArray& status, const QByteArray& type, const QByteArray& body) {
        s->write("HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) +
                 "\r\nCache-Control: no-store\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n" + body);
        s->disconnectFromHost();
    };
    if (line.value(0) != "GET") { reply("405 Method Not Allowed", "text/plain", "GET only\n"); return; }
    if (path == "/" || path == "/index.html") { reply("200 OK", "text/html; charset=utf-8", pageHtml("stream.mjpg", "VeyAmux")); return; }
    if (path == "/ping") { reply("200 OK", "text/plain", "ok\n"); return; }
    if (path == "/snapshot.jpg") {
        if (lastJpeg_.isEmpty()) reply("503 Service Unavailable", "text/plain", "no picture yet\n");
        else reply("200 OK", "image/jpeg", lastJpeg_);
        return;
    }
    if (path == "/stream.mjpg") {
        s->write("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=veyamuxframe\r\nCache-Control: no-store\r\n"
                 "Access-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        streams_.push_back(s);
        if (!lastJpeg_.isEmpty()) {   // show the current picture right away
            s->write("--veyamuxframe\r\nContent-Type: image/jpeg\r\nContent-Length: " + QByteArray::number(lastJpeg_.size()) + "\r\n\r\n");
            s->write(lastJpeg_);
            s->write("\r\n");
        }
        viewers_ = int(std::count_if(streams_.begin(), streams_.end(), [](const QPointer<QTcpSocket>& p) { return !p.isNull(); }));
        emit viewersChanged();
        connect(s, &QTcpSocket::disconnected, this, [this] {
            streams_.erase(std::remove_if(streams_.begin(), streams_.end(),
                                          [](const QPointer<QTcpSocket>& p) { return p.isNull() || p->state() != QAbstractSocket::ConnectedState; }),
                           streams_.end());
            viewers_ = int(streams_.size());
            emit viewersChanged();
        });
        return;
    }
    reply("404 Not Found", "text/plain", "not found\n");
}

QByteArray ObsStream::pageHtml(const QString& streamUrl, const QString& title) {
    // The picture fills the browser source (letterboxed, transparent around it). Nothing else is ever drawn - this page
    // ends up in the stream. A ping every 1.5 s notices when VeyAmux is closed and reconnects when it is back.
    const QString html = QStringLiteral(R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><title>%1</title>
<style>
html,body{margin:0;height:100%;background:transparent;overflow:hidden}
#v{position:fixed;inset:0;width:100%;height:100%;object-fit:contain;display:block;visibility:hidden}
</style></head>
<body><img id="v" alt="">
<script>
const stream = "%2";
const ping = stream.replace(/stream\.mjpg$/, "ping");
const v = document.getElementById("v");
let alive = false;
v.onload = () => { v.style.visibility = "visible"; };
v.onerror = () => { v.style.visibility = "hidden"; alive = false; };
async function check() {
  try {
    const r = await fetch(ping + "?t=" + Date.now(), {cache: "no-store"});
    if (!r.ok) throw new Error();
    if (!alive) { alive = true; v.src = stream + "?t=" + Date.now(); }
  } catch (e) {
    if (alive || v.src) { alive = false; v.removeAttribute("src"); v.style.visibility = "hidden"; }
  }
}
setInterval(check, 1500);
check();
</script></body></html>
)HTML");
    return html.arg(title.toHtmlEscaped(), streamUrl).toUtf8();
}
