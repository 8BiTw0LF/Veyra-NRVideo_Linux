// VeyAmux for Linux: screen share portal (see ScreenCastPortal.h).
#include "ScreenCastPortal.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusVariant>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QRandomGenerator>

#include <fcntl.h>
#include <unistd.h>

namespace veyamux {

// ------------------------------------------------------------------ the portal

namespace {
const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kIface = QStringLiteral("org.freedesktop.portal.ScreenCast");
}

ScreenCastPortal::ScreenCastPortal(QObject* parent) : QObject(parent) {}
ScreenCastPortal::~ScreenCastPortal() { closeSession(); }

QString ScreenCastPortal::requestPath(const QString& token) const {
    QString sender = QDBusConnection::sessionBus().baseService().mid(1);
    sender.replace('.', '_');
    return kPath + "/request/" + sender + "/" + token;
}

void ScreenCastPortal::closeSession() {
    if (!pendingPath_.isEmpty()) {
        QDBusConnection::sessionBus().disconnect(kService, pendingPath_, "org.freedesktop.portal.Request", "Response",
                                                 this, SLOT(onResponse(uint, QVariantMap)));
        pendingPath_.clear();
    }
    if (!session_.isEmpty()) {
        QDBusMessage m = QDBusMessage::createMethodCall(kService, session_, "org.freedesktop.portal.Session", "Close");
        QDBusConnection::sessionBus().asyncCall(m);
        session_.clear();
    }
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    step_ = 0;
}

bool ScreenCastPortal::call(const QString& method, const QList<QVariant>& args, const QString& token) {
    auto bus = QDBusConnection::sessionBus();
    if (!pendingPath_.isEmpty())
        bus.disconnect(kService, pendingPath_, "org.freedesktop.portal.Request", "Response", this, SLOT(onResponse(uint, QVariantMap)));
    pendingPath_ = requestPath(token);
    // subscribe before calling, so a quick answer is not missed
    bus.connect(kService, pendingPath_, "org.freedesktop.portal.Request", "Response", this, SLOT(onResponse(uint, QVariantMap)));
    QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, kIface, method);
    m.setArguments(args);
    const QDBusMessage r = bus.call(m, QDBus::Block, 10000);
    if (r.type() == QDBusMessage::ErrorMessage) { fail("Screen share portal: " + r.errorMessage()); return false; }
    return true;
}

void ScreenCastPortal::start(const QString& restoreToken) {
    closeSession();
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || !bus.interface()->isServiceRegistered(kService).value()) {
        fail("The screen share portal (xdg-desktop-portal) is not running");
        return;
    }
    restoreToken_ = restoreToken;
    const QString t = QStringLiteral("veyamux%1").arg(QRandomGenerator::global()->generate());
    QVariantMap o;
    o["handle_token"] = t;
    o["session_handle_token"] = QStringLiteral("veyamuxs%1").arg(++counter_);
    step_ = 1;
    call("CreateSession", {o}, t);
}

void ScreenCastPortal::fail(const QString& msg, bool cancelled) {
    closeSession();
    emit failed(msg, cancelled);
}

void ScreenCastPortal::onResponse(uint response, const QVariantMap& results) {
    if (response != 0) { fail(response == 1 ? "Cancelled" : "The screen share was not started", response == 1); return; }
    if (step_ == 1) {
        session_ = results.value("session_handle").toString();
        if (session_.isEmpty()) session_ = results.value("session_handle").value<QDBusObjectPath>().path();
        if (session_.isEmpty()) { fail("Screen share portal: no session"); return; }
        // properties read with Properties.Get (QDBusInterface::property needs them in the introspection data)
        auto prop = [](const char* name) -> uint {
            QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, "org.freedesktop.DBus.Properties", "Get");
            m.setArguments({kIface, QString::fromLatin1(name)});
            const QDBusMessage r = QDBusConnection::sessionBus().call(m, QDBus::Block, 3000);
            if (r.type() != QDBusMessage::ReplyMessage || r.arguments().isEmpty()) return 0;
            return r.arguments().first().value<QDBusVariant>().variant().toUInt();
        };
        const uint version = prop("version");
        uint types = prop("AvailableSourceTypes");
        types = types ? (types & 3u) : 3u;                 // 1 = screen, 2 = window
        const uint cursors = prop("AvailableCursorModes");
        const QString t = QStringLiteral("veyamux%1").arg(QRandomGenerator::global()->generate());
        QVariantMap o;
        o["handle_token"] = t;
        o["types"] = QVariant::fromValue<uint>(types ? types : 3u);
        o["multiple"] = false;
        o["cursor_mode"] = QVariant::fromValue<uint>((cursors & 2u) ? 2u : 1u);   // cursor drawn into the picture
        if (version >= 4) {
            o["persist_mode"] = QVariant::fromValue<uint>(2);   // remember the choice until revoked
            if (!restoreToken_.isEmpty()) o["restore_token"] = restoreToken_;
        }
        step_ = 2;
        call("SelectSources", {QVariant::fromValue(QDBusObjectPath(session_)), o}, t);
    } else if (step_ == 2) {
        const QString t = QStringLiteral("veyamux%1").arg(QRandomGenerator::global()->generate());
        QVariantMap o;
        o["handle_token"] = t;
        step_ = 3;
        call("Start", {QVariant::fromValue(QDBusObjectPath(session_)), QString(), o}, t);
    } else if (step_ == 3) {
        uint node = 0;
        int w = 0, h = 0;
        const QDBusArgument arg = results.value("streams").value<QDBusArgument>();
        arg.beginArray();
        while (!arg.atEnd()) {
            uint n = 0;
            QVariantMap props;
            arg.beginStructure();
            arg >> n >> props;
            arg.endStructure();
            if (!node) {
                node = n;
                const QDBusArgument sz = props.value("size").value<QDBusArgument>();
                if (sz.currentType() == QDBusArgument::StructureType) { sz.beginStructure(); sz >> w >> h; sz.endStructure(); }
            }
        }
        arg.endArray();
        if (!node) { fail("Nothing was picked to share"); return; }
        QDBusMessage m = QDBusMessage::createMethodCall(kService, kPath, kIface, "OpenPipeWireRemote");
        m.setArguments({QVariant::fromValue(QDBusObjectPath(session_)), QVariantMap()});
        const QDBusReply<QDBusUnixFileDescriptor> r = QDBusConnection::sessionBus().call(m, QDBus::Block, 10000);
        if (!r.isValid() || !r.value().isValid()) { fail("Screen share portal: no PipeWire connection"); return; }
        fd_ = fcntl(r.value().fileDescriptor(), F_DUPFD_CLOEXEC, 3);
        step_ = 0;
        const QString token = results.value("restore_token").toString();
        emit ready(fd_, node, w, h, token);
    }
}

} // namespace veyamux
