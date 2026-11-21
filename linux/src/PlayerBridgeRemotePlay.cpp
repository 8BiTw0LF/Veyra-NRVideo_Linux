// VeyAmux for Linux: the Qt side of PlayStation Remote Play (RemotePlay.h): the consoles, the PSN login,
// registration, wake-up and starting the stream. Registered consoles are kept in QSettings under ps/consoles/<id>.
#include "PlayerBridge.h"
#include "RemotePlay.h"

#include <QDesktopServices>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QVariantMap>

#include <chrono>

namespace rp = veyamux::remoteplay;

namespace {

rp::Registered loadConsole(const QString& id) {
    QSettings q;
    q.beginGroup("ps/consoles/" + id);
    rp::Registered r;
    r.id = id.toStdString();
    r.name = q.value("name").toString().toStdString();
    r.host = q.value("host").toString().toStdString();
    r.ps5 = q.value("ps5").toBool();
    r.target = q.value("target").toInt();
    r.registKey = q.value("registKey").toString().toStdString();
    r.morning = q.value("morning").toString().toStdString();
    return r;
}

void saveConsole(const rp::Registered& r) {
    QSettings q;
    q.beginGroup("ps/consoles/" + QString::fromStdString(r.id));
    q.setValue("name", QString::fromStdString(r.name));
    q.setValue("host", QString::fromStdString(r.host));
    q.setValue("ps5", r.ps5);
    q.setValue("target", r.target);
    q.setValue("registKey", QString::fromStdString(r.registKey));
    q.setValue("morning", QString::fromStdString(r.morning));
}

QStringList registeredIds() {
    QSettings q;
    q.beginGroup("ps/consoles");
    return q.childGroups();
}

// consoles typed in by address: "address|ps5" (ps5 = 1 / 0)
struct Manual { QString host; bool ps5; };
QList<Manual> manualHosts() {
    QList<Manual> out;
    for (const QString& e : QSettings().value("ps/manualHosts").toStringList()) {
        const QStringList p = e.split('|');
        if (!p.isEmpty() && !p[0].isEmpty()) out << Manual{p[0], p.value(1) != "0"};
    }
    return out;
}
void pushManualHosts() {
    std::vector<std::string> v;
    for (const Manual& m : manualHosts()) v.push_back(m.host.toStdString());
    rp::setManualHosts(v);
}

const rp::Console* findSeen(const std::vector<rp::Console>& list, const std::string& id) {
    for (const auto& c : list) if (c.id == id) return &c;
    return nullptr;
}

} // namespace

// ------------------------------------------------------------------ state

QVariantList PlayerBridge::psConsoles() const {
    QVariantList out;
    const auto seen = rp::consoles();
    QSettings q;
    QStringList done;
    for (const QString& id : registeredIds()) {
        q.beginGroup("ps/consoles/" + id);
        QVariantMap m;
        m["id"] = id;
        m["name"] = q.value("name").toString();
        m["host"] = q.value("host").toString();
        m["ps5"] = q.value("ps5").toBool();
        m["registered"] = true;
        m["passcode"] = q.value("passcode").toString();
        q.endGroup();
        const rp::Console* c = findSeen(seen, id.toStdString());
        m["found"] = c != nullptr;
        m["ready"] = c && c->ready;
        m["app"] = c ? QString::fromStdString(c->runningApp) : QString();
        if (c) m["host"] = QString::fromStdString(c->host);
        out << m;
        done << id;
    }
    for (const auto& c : seen) {
        const QString id = QString::fromStdString(c.id);
        if (done.contains(id)) continue;
        QVariantMap m;
        m["id"] = id;
        m["name"] = QString::fromStdString(c.name);
        m["host"] = QString::fromStdString(c.host);
        m["ps5"] = c.ps5;
        m["registered"] = false;
        m["found"] = true;
        m["ready"] = c.ready;
        m["app"] = QString::fromStdString(c.runningApp);
        m["passcode"] = QString();
        out << m;
    }
    // addresses typed in that no console has answered from yet (and no registered console uses)
    for (const Manual& mh : manualHosts()) {
        bool known = false;
        for (const QVariant& v : out) if (v.toMap().value("host").toString() == mh.host) known = true;
        if (known) continue;
        QVariantMap m;
        m["id"] = "ip:" + mh.host;
        m["name"] = mh.host;
        m["host"] = mh.host;
        m["ps5"] = mh.ps5;
        m["registered"] = false;
        m["found"] = false;
        m["ready"] = false;
        m["app"] = QString();
        m["passcode"] = QString();
        m["manual"] = true;
        out << m;
    }
    return out;
}

bool PlayerBridge::psAddHost(const QString& address, bool ps5) {
    QString a = address.trimmed();
    a.remove(QRegularExpression("^[a-z]+://")).remove(QRegularExpression("[:/].*$"));   // "http://x/" or "x:9295"
    static const QRegularExpression ok(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9.-]{0,252}$"));
    if (!ok.match(a).hasMatch()) { psSay(L("Type the console's IP address, for example 192.168.1.50", "Skriv konsollens IP-adresse, for eksempel 192.168.1.50"), true); return false; }
    QStringList list;
    for (const Manual& m : manualHosts()) if (m.host != a) list << m.host + "|" + (m.ps5 ? "1" : "0");
    list << a + "|" + (ps5 ? "1" : "0");
    QSettings().setValue("ps/manualHosts", list);
    pushManualHosts();
    rp::startDiscovery();
    psSay(L("Asking %1 directly. If it answers, it shows its name and state", "Spørger %1 direkte. Hvis den svarer, vises dens navn og tilstand").arg(a), false);
    return true;
}

void PlayerBridge::psRemoveHost(const QString& address) {
    QStringList list;
    for (const Manual& m : manualHosts()) if (m.host != address) list << m.host + "|" + (m.ps5 ? "1" : "0");
    QSettings().setValue("ps/manualHosts", list);
    pushManualHosts();
    emit psChanged();
}

bool PlayerBridge::psHasAccount() const { return !QSettings().value("ps/accountId").toString().isEmpty(); }
int PlayerBridge::psHeight() const { return QSettings().value("ps/height", 1080).toInt() <= 720 ? 720 : 1080; }
void PlayerBridge::setPsHeight(int h) { QSettings().setValue("ps/height", h <= 720 ? 720 : 1080); emit psChanged(); }
int PlayerBridge::psFps() const { return QSettings().value("ps/fps", 60).toInt() <= 30 ? 30 : 60; }
void PlayerBridge::setPsFps(int f) { QSettings().setValue("ps/fps", f <= 30 ? 30 : 60); emit psChanged(); }
bool PlayerBridge::psHdr() const { return QSettings().value("ps/hdr", false).toBool(); }
void PlayerBridge::setPsHdr(bool v) { QSettings().setValue("ps/hdr", v); emit psChanged(); }
bool PlayerBridge::psActive() const { return engine_.isOpen() && engine_.info().videoCodec == "Remote Play"; }

void PlayerBridge::psSay(const QString& message, bool error) {
    psMessage_ = message;
    psMessageError_ = error;
    emit psChanged();
}

void PlayerBridge::psRun(const QString& busy, std::function<void(QString&, bool&)> work, std::function<void()> done) {
    if (!psBusy_.isEmpty()) return;
    if (psWorker_.joinable()) psWorker_.join();
    psBusy_ = busy;
    psMessage_.clear();
    emit psChanged();
    psWorker_ = std::thread([this, quit = psQuit_, work = std::move(work), done = std::move(done)] {
        QString message;
        bool error = false;
        work(message, error);
        if (quit->load()) return;   // VeyAmux closed meanwhile
        QMetaObject::invokeMethod(this, [this, message, error, done] {
            psBusy_.clear();
            psSay(message, error);
            if (done) done();
        }, Qt::QueuedConnection);
    });
}

// ------------------------------------------------------------------ discovery

void PlayerBridge::psStartSearch() {
    pushManualHosts();
    rp::startDiscovery();
    if (!psTimer_) {
        psTimer_ = new QTimer(this);
        psTimer_->setInterval(1000);
        connect(psTimer_, &QTimer::timeout, this, [this] {   // only when the list really changed
            const QVariantList now = psConsoles();
            if (now != psSeen_) { psSeen_ = now; emit psChanged(); }
        });
    }
    psTimer_->start();
    emit psChanged();
}

void PlayerBridge::psStopSearch() {
    if (psTimer_) psTimer_->stop();
    // (discovery keeps running while a console is being woken)
    if (psBusy_.isEmpty()) rp::stopDiscovery();
}

// ------------------------------------------------------------------ PSN account

void PlayerBridge::psOpenLogin() { QDesktopServices::openUrl(QUrl(QString::fromStdString(rp::psnLoginUrl()))); }

void PlayerBridge::psSetLogin(const QString& input) {
    const QString t = input.trimmed();
    // an account ID typed in directly: 8 bytes in base64
    if (!t.contains("code=") && QByteArray::fromBase64(t.toLatin1()).size() == 8) {
        QSettings().setValue("ps/accountId", t);
        psSay(L("PlayStation account saved", "PlayStation-konto gemt"), false);
        return;
    }
    const std::string url = t.toStdString();
    psRun(L("Asking PlayStation Network for your account ID...", "Spørger PlayStation Network om dit konto-ID..."),
          [this, url](QString& msg, bool& err) {
              std::string id, e;
              if (rp::psnAccountId(url, id, e)) {
                  const QString acc = QString::fromStdString(id);
                  QMetaObject::invokeMethod(this, [acc] { QSettings().setValue("ps/accountId", acc); }, Qt::QueuedConnection);
                  msg = L("PlayStation account saved", "PlayStation-konto gemt");
              } else {
                  msg = L("Login failed: ", "Login mislykkedes: ") + translateMessage(QString::fromStdString(e));
                  err = true;
              }
          });
}

// ------------------------------------------------------------------ registration

void PlayerBridge::psRegister(const QString& id, const QString& pin) {
    const QString digits = QString(pin).remove(QRegularExpression("[^0-9]"));
    if (digits.size() != 8) { psSay(L("The PIN has 8 digits", "PIN-koden har 8 cifre"), true); return; }
    const auto seen = rp::consoles();
    const rp::Console* c = findSeen(seen, id.toStdString());
    rp::Console console;
    if (c) {
        console = *c;
    } else if (id.startsWith("ip:")) {   // typed in, not answering the search: register straight at the address
        console.host = id.mid(3).toStdString();
        for (const Manual& m : manualHosts()) if (m.host == id.mid(3)) console.ps5 = m.ps5;
    } else {
        psSay(L("The console is not on the network any more", "Konsollen er ikke på netværket længere"), true);
        return;
    }
    const std::string account = QSettings().value("ps/accountId").toString().toStdString();
    if (account.empty()) { psSay(L("Log in to PlayStation Network first", "Log ind på PlayStation Network først"), true); return; }
    const uint32_t p = digits.toUInt();
    psRun(L("Registering with the console...", "Registrerer hos konsollen..."), [this, console, account, p](QString& msg, bool& err) {
        rp::Registered r;
        std::string e;
        if (rp::registerConsole(console, account, p, r, e)) {
            QMetaObject::invokeMethod(this, [this, r] { saveConsole(r); emit psChanged(); }, Qt::QueuedConnection);
            msg = L("Registered: ", "Registreret: ") + QString::fromStdString(r.name);
        } else {
            msg = translateMessage(QString::fromStdString(e));
            err = true;
        }
    });
}

void PlayerBridge::psSetPasscode(const QString& id, const QString& passcode) {
    QSettings().setValue("ps/consoles/" + id + "/passcode", QString(passcode).remove(QRegularExpression("[^0-9]")));
    emit psChanged();
}

void PlayerBridge::psForget(const QString& id) {
    QSettings q;
    q.remove("ps/consoles/" + id);
    emit psChanged();
}

// ------------------------------------------------------------------ connecting

bool PlayerBridge::psStart(const QString& id, QString& error) {
    rp::StreamConfig cfg;
    cfg.console = loadConsole(id);
    if (cfg.console.registKey.empty()) { error = L("Register the console first", "Registrér konsollen først"); return false; }
    // the address may have changed since the registration (DHCP)
    for (const auto& c : rp::consoles()) if (c.id == cfg.console.id) cfg.console.host = c.host;
    cfg.height = psHeight();
    cfg.fps = psFps();
    cfg.hdr = cfg.console.ps5 && psHdr();
    cfg.passcode = QSettings().value("ps/consoles/" + id + "/passcode").toString().toStdString();
    rp::setStreamConfig(cfg);
    if (portal_) portal_->closeSession();
    if (!startCapture("ps:" + id, veyamux::CaptureMode{}, QStringLiteral("@ps"))) { error = status_; return false; }
    return true;
}

void PlayerBridge::psConnect(const QString& id) {
    const rp::Registered r = loadConsole(id);
    if (r.registKey.empty()) { psSay(L("Register the console first", "Registrér konsollen først"), true); return; }
    rp::startDiscovery();   // to see when it is awake
    const auto seen = rp::consoles();
    const rp::Console* c = findSeen(seen, r.id);
    if (c && c->ready) {
        QString e;
        if (!psStart(id, e)) psSay(e, true);
        else psSay({}, false);
        return;
    }
    // in rest mode (or not seen yet): wake it and wait until it answers as ready
    psRun(L("Waking the console...", "Vækker konsollen..."), [r, quit = psQuit_](QString& msg, bool& err) {
        std::string e;
        if (!rp::wake(r, e)) { msg = QString::fromStdString(e); err = true; return; }   // (translated in the done step)
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        int resend = 0;
        bool seenAny = false;
        while (std::chrono::steady_clock::now() < until && !quit->load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            for (const auto& s : rp::consoles()) if (s.id == r.id) { seenAny = true; if (s.ready) return; }
            if (++resend % 10 == 0) rp::wake(r, e);   // again every 5 s
            if (!seenAny && resend >= 16) break;      // not visible to discovery at all: do not wait the full time
        }
        // not seen as ready (another network segment, or discovery blocked by a firewall): connect anyway; if the
        // console really is asleep, the connection fails with the console's own reason
    }, [this, id] {
        if (psMessageError_) { psSay(translateMessage(psMessage_), true); return; }
        QString e;
        if (!psStart(id, e)) psSay(e, true);
    });
}

void PlayerBridge::psPad(quint32 buttons, double lx, double ly, double rx, double ry, double l2, double r2) {
    rp::setController(buttons, lx, ly, rx, ry, l2, r2);
}
