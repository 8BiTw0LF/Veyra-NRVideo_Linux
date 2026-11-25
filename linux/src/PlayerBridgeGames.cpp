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

void PlayerBridge::openGame(const QString& core, const QString& rom, const QString& title, const QString& systemId, const QString& biosDir) {
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
    if (retroActive_) {
        veyamux::retro::setPad(veyamux::retro::fromPlayStation(buttons, l2, r2), lx, ly, rx, ry);
        return;
    }
    rp::setController(buttons, lx, ly, rx, ry, l2, r2);
}

// done / failed messages of the emulator (state saved, ...), shown as a notice
void PlayerBridge::retroNextDisc() { if (retroActive()) veyamux::retro::nextDisc(); }

void PlayerBridge::pollRetroMessage() {
    const int n = retroActive_ ? veyamux::retro::discCount() : 0, i = retroActive_ ? veyamux::retro::discIndex() + 1 : 0;
    if (n != retroDiscs_ || i != retroDisc_) { retroDiscs_ = n; retroDisc_ = i; emit retroDiscChanged(); }
    if (!retroActive_) return;
    const std::string m = veyamux::retro::takeMessage();
    if (!m.empty()) showNotice(translateMessage(QString::fromStdString(m)));
}
