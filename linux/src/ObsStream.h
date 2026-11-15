// VeyAmux for Linux - local stream of the finished picture for an OBS "Browser" source.
//
// A tiny HTTP server on 127.0.0.1 (never reachable from other machines):
//   GET /             the player page (same as the saved browser file)
//   GET /stream.mjpg  multipart/x-mixed-replace MJPEG: every frame VeyAmux shows (Lossless Scaling frames are made later, in the driver layer), paced like
//                     on screen
//   GET /snapshot.jpg the latest frame
// The browser file the user saves is a self-contained HTML page pointing at http://127.0.0.1:<port>/stream.mjpg; in OBS
// it is added as a Browser source with "Local file". Frames are only produced (and read back from the GPU) while at
// least one viewer is connected. Encoding runs on two worker threads, latest-frame-wins: a slow viewer never slows
// VeyAmux down, it just gets fewer frames.
#pragma once
#include <QByteArray>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

class QTcpServer;
class QTcpSocket;

class ObsStream : public QObject {
    Q_OBJECT
public:
    explicit ObsStream(QObject* parent = nullptr);
    ~ObsStream() override;

    bool start(quint16 port, QString* error);
    void stop();
    bool running() const;
    quint16 port() const { return port_; }
    int viewers() const { return viewers_.load(); }
    bool wantsFrames() const { return viewers_.load() > 0; }

    // Largest picture height sent (0 = original size); width follows the aspect ratio.
    void setMaxHeight(int h) { maxHeight_ = h; }
    int maxHeight() const { return maxHeight_.load(); }
    // Fits (w, h) into the max height, even dimensions.
    QSize targetSize(int w, int h) const;

    // Thread-safe, any thread. delayMs: show this frame that much later (keeps frame-generation pacing).
    void pushFrame(QImage img, int delayMs = 0);

    // The browser page; streamUrl absolute for the saved file, relative when served by us.
    static QByteArray pageHtml(const QString& streamUrl, const QString& title);

signals:
    void viewersChanged();

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket* s);
    void broadcast(quint64 seq, QByteArray jpeg);
    void encoderLoop();

    QTcpServer* server_ = nullptr;
    quint16 port_ = 0;
    std::vector<QPointer<QTcpSocket>> streams_;
    QByteArray lastJpeg_;
    quint64 lastSent_ = 0;
    std::atomic<int> viewers_{0};
    std::atomic<int> maxHeight_{1080};

    struct Job { QImage img; quint64 seq = 0; std::chrono::steady_clock::time_point due; };
    std::mutex m_;
    std::condition_variable cv_;
    std::deque<Job> jobs_;
    quint64 nextSeq_ = 1;
    bool quit_ = false;
    std::vector<std::thread> encoders_;
};
