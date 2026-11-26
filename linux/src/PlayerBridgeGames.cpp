// VeyAmux for Linux: PlayerBridge - games from the Games page (Libretro.h, GameLibrary.h).
#include "PlayerBridge.h"
#include "GameLibrary.h"
#include "Libretro.h"
#include "RemotePlay.h"
#include <QFileInfo>
#include <QHash>
#include <QScreen>

#include <QDir>
#include <QStandardPaths>

namespace rp = veyamux::remoteplay;

void PlayerBridge::openGame(const QString& core, const QString& rom, const QString& title, const QString& systemId, const QString& biosDir,
                            bool resume) {
    const veyamux::GameSystem* sys = veyamux::gameSystem(systemId);
    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/games/";
    veyamux::retro::Game g;
    g.core = core.toStdString();
    g.rom = rom.toStdString();
    g.title = title.toStdString();
    g.systemDir = biosDir.toStdString();
    g.saveDir = (data + "saves/" + systemId).toStdString();
    g.stateDir = (data + "states/" + systemId).toStdString();
    g.stickAsDpad = !sys || !sys->analog;
    retroSystem_ = systemId;
    retroRewindKey_ = g.stickAsDpad;   // L2 is free on systems without analog sticks
    retroRewinding_ = false;
    g.runAhead = retroRunAhead();
    g.rewind = retroRewind();
    g.resume = resume;
    emit retroOptionsChanged();
    emit playersReset();
    veyamux::retro::setGame(g);
    veyamux::retro::setRumbleCallback([this](int low, int high) {
        QMetaObject::invokeMethod(this, [this, low, high] { emit psRumble(low, high); }, Qt::QueuedConnection);
    });
    if (portal_) portal_->closeSession();
    if (startCapture("retro:" + systemId, veyamux::CaptureMode{}, QStringLiteral("@retro")))
        qInfo("Games: %s (%s)", qPrintable(QFileInfo(rom).fileName()), qPrintable(QFileInfo(core).fileName()));
}

void PlayerBridge::retroSaveState() { if (retroActive()) veyamux::retro::saveState(0); }
void PlayerBridge::retroLoadState() { if (retroActive()) veyamux::retro::loadState(0); }
void PlayerBridge::retroReset() { if (retroActive()) veyamux::retro::reset(); }
void PlayerBridge::retroPause(bool paused) { veyamux::retro::setPaused(paused && retroActive()); }

void PlayerBridge::retroKey(const QString& key, bool down) {
    // libretro's joypad numbers: B 0, Y 1, Select 2, Start 3, Up 4, Down 5, Left 6, Right 7, A 8, X 9, L 10, R 11
    static const QHash<QString, int> ids = {{"b", 0}, {"y", 1}, {"select", 2}, {"start", 3}, {"up", 4}, {"down", 5}, {"left", 6},
                                            {"right", 7}, {"a", 8}, {"x", 9}, {"l", 10}, {"r", 11}};
    const int id = ids.value(key, -1);
    if (id < 0) return;
    if (down) retroKeys_ |= 1u << id;
    else retroKeys_ &= ~(1u << id);
    veyamux::retro::setKeys(retroKeys_);
}

// the controller: to the console (Remote Play) or to the game (libretro)
void PlayerBridge::psPad(quint32 buttons, double lx, double ly, double rx, double ry, double l2, double r2) {
    if (retroActive_) return;   // a game takes each controller as its own player (psPlayer)
    rp::setController(buttons, lx, ly, rx, ry, l2, r2);
}

// players 1-4 of a game. On a system without L2 (NES, SNES, Mega Drive, Game Boy, ...) player 1's L2 rewinds while
// held, when rewind is on.
void PlayerBridge::psPlayer(int player, quint32 buttons, double lx, double ly, double rx, double ry, double l2, double r2) {
    if (!retroActive_) return;
    if (player == 0 && retroRewindKey_ && retroRewind()) {
        // pressed past 3/4, let go below 1/4: a trigger that rests near half way (or a noisy one) cannot flicker it -
        // stepping back while a button is held makes the game see that button pressed again
        if (!retroRewinding_ && l2 > 0.75) retroRewinding_ = true;
        else if (retroRewinding_ && l2 < 0.25) retroRewinding_ = false;
        veyamux::retro::setRewinding(retroRewinding_);
        l2 = 0;
    }
    veyamux::retro::setPad(player, veyamux::retro::fromPlayStation(buttons, l2, r2), lx, ly, rx, ry);
}

// run-ahead and rewind, kept per system (games/<system>/runAhead, .../rewind); the defaults: 1 frame and rewind on for
// cartridge systems, off for CD systems (their states are large and the emulators heavier)
static bool cdSystem(const QString& s) { return s == "psx" || s == "saturn" || s == "segacd" || s == "pcecd"; }
int PlayerBridge::retroRunAhead() const {
    return QSettings().value("games/" + retroSystem_ + "/runAhead", cdSystem(retroSystem_) ? 0 : 1).toInt();
}
void PlayerBridge::setRetroRunAhead(int frames) {
    QSettings().setValue("games/" + retroSystem_ + "/runAhead", frames);
    veyamux::retro::setRunAhead(frames);
    emit retroOptionsChanged();
}
bool PlayerBridge::retroRewind() const {
    return QSettings().value("games/" + retroSystem_ + "/rewind", !cdSystem(retroSystem_)).toBool();
}
void PlayerBridge::setRetroRewind(bool on) {
    QSettings().setValue("games/" + retroSystem_ + "/rewind", on);
    veyamux::retro::setRewind(on);
    if (!on) { veyamux::retro::setRewinding(false); retroRewinding_ = false; }
    emit retroOptionsChanged();
}
void PlayerBridge::retroRewindHold(bool held) { if (retroActive() && retroRewind()) veyamux::retro::setRewinding(held); }

// done / failed messages of the emulator (state saved, ...), shown as a notice
void PlayerBridge::retroNextDisc() { if (retroActive()) veyamux::retro::nextDisc(); }

void PlayerBridge::pollRetroMessage() {
    const int n = retroActive_ ? veyamux::retro::discCount() : 0, i = retroActive_ ? veyamux::retro::discIndex() + 1 : 0;
    if (n != retroDiscs_ || i != retroDisc_) { retroDiscs_ = n; retroDisc_ = i; emit retroDiscChanged(); }
    if (!retroActive_) return;
    const std::string m = veyamux::retro::takeMessage();
    if (!m.empty()) showNotice(translateMessage(QString::fromStdString(m)));
}
