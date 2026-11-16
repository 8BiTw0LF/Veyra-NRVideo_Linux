// VeyAmux for Linux: the compositor's "share a window or screen" dialog (xdg-desktop-portal ScreenCast over D-Bus).
// Hands back a PipeWire remote fd and the node id that ScreenCastStream (ScreenCast.h) reads.
#pragma once
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace veyamux {

// ---- the portal dialog (main thread, asynchronous)
class ScreenCastPortal : public QObject {
    Q_OBJECT
public:
    explicit ScreenCastPortal(QObject* parent = nullptr);
    ~ScreenCastPortal() override;
    // Opens the compositor's "share a window or screen" dialog. restoreToken (from an earlier share) lets the
    // compositor reuse the last choice without asking again.
    void start(const QString& restoreToken);
    void closeSession();
    bool busy() const { return step_ != 0; }
    int fd() const { return fd_; }

signals:
    // node: PipeWire node id of the picked window/screen; fd stays owned by the portal object until the next start.
    void ready(int fd, uint node, int width, int height, const QString& restoreToken);
    void failed(const QString& message, bool cancelled);

private slots:
    void onResponse(uint response, const QVariantMap& results);

private:
    bool call(const QString& method, const QList<QVariant>& args, const QString& token);
    QString requestPath(const QString& token) const;
    void fail(const QString& msg, bool cancelled = false);
    int step_ = 0;               // 0 idle, 1 CreateSession, 2 SelectSources, 3 Start
    QString session_;
    QString pendingPath_;
    QString restoreToken_;
    int fd_ = -1;
    uint counter_ = 0;
};

} // namespace veyamux
