// VeyAmux for Linux: game controllers for the menu (see Gamepad.h).
#include "Gamepad.h"

#include <QDir>
#include <QFile>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

namespace veyamux {

namespace remoteplay_bits {   // = remoteplay::Button (RemotePlay.h), kept here so the menu code needs no chiaki
constexpr quint32 kCross = 1u << 0, kCircle = 1u << 1, kSquare = 1u << 2, kTriangle = 1u << 3, kLeft = 1u << 4,
                  kRight = 1u << 5, kUp = 1u << 6, kDown = 1u << 7, kL1 = 1u << 8, kR1 = 1u << 9, kL3 = 1u << 10,
                  kR3 = 1u << 11, kOptions = 1u << 12, kShare = 1u << 13, kTouchpad = 1u << 14, kPs = 1u << 15;
}

namespace {
constexpr int kRepeatDelay = 380, kRepeatRate = 110;   // ms
constexpr int kGuideHold = 500;                         // ms
inline bool testBit(const unsigned long* bits, int bit) {
    return bits[bit / (8 * sizeof(long))] & (1UL << (bit % (8 * sizeof(long))));
}
} // namespace

GamepadInput::GamepadInput(QObject* parent) : QObject(parent) {
    scanTimer_.setInterval(2000);
    connect(&scanTimer_, &QTimer::timeout, this, &GamepadInput::scan);
    repeat_.setSingleShot(false);
    connect(&repeat_, &QTimer::timeout, this, [this] {
        if (heldDir_.isEmpty()) { repeat_.stop(); return; }
        repeat_.setInterval(kRepeatRate);
        emit button(heldDir_);
    });
    clock_.start();
    guideTimer_.setSingleShot(true);
    guideTimer_.setInterval(kGuideHold);
    connect(&guideTimer_, &QTimer::timeout, this, [this] { if (guide_) { comboFired_ = true; emit menuCombo(); } });
    // tests: one or more pipes with raw input_events (colon-separated)
    for (const QString& fifo : qEnvironmentVariable("VEYAMUX_TEST_PAD_FIFO").split(':', Qt::SkipEmptyParts)) addPad(fifo, true);
    scan();
    scanTimer_.start();
}

GamepadInput::~GamepadInput() {
    for (Pad* p : pads_) {
        if (p->grabbedNow) ioctl(p->fd, EVIOCGRAB, 0);
        ::close(p->fd);
        delete p;
    }
}

QStringList GamepadInput::controllers() const {
    QStringList n;
    for (const Pad* p : pads_) if (!p->touchpad) n << p->name;
    n.sort();
    return n;
}

void GamepadInput::scan() {
    const QStringList now = QDir("/dev/input").entryList({"event*"}, QDir::System | QDir::Files);
    for (const QString& e : now) {
        const QString path = "/dev/input/" + e;
        if (!pads_.contains(path) && !ignored_.contains(path)) addPad(path, false);
    }
    for (auto it = ignored_.begin(); it != ignored_.end();) it = QFile::exists(*it) ? std::next(it) : ignored_.erase(it);
    for (const QString& path : pads_.keys())
        if (!pads_[path]->test && !QFile::exists(path)) removePad(path);
}

void GamepadInput::addPad(const QString& path, bool test) {
    // (a test FIFO is opened read-write, so it never reports end-of-file when the writer pauses)
    const int fd = ::open(path.toLocal8Bit().constData(), (test ? O_RDWR : O_RDONLY) | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { ignored_.insert(path); return; }   // no access (not a controller, or another user's device)
    auto* p = new Pad;
    p->fd = fd;
    p->path = path;
    p->test = test;
    if (!test) {
        // a controller has the gamepad buttons (BTN_SOUTH = BTN_GAMEPAD); keyboards and mice do not
        unsigned long keys[(KEY_MAX + 1) / (8 * sizeof(long)) + 1]{};
        char name[128]{};
        const bool keysOk = ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0;
        ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
        p->name = QString::fromUtf8(name).trimmed();
        // a PlayStation controller's touchpad is its own device; its click is the touchpad button (Remote Play)
        p->touchpad = keysOk && !testBit(keys, BTN_SOUTH) && testBit(keys, BTN_LEFT) && p->name.contains("Touchpad", Qt::CaseInsensitive)
                      && (p->name.contains("DualSense", Qt::CaseInsensitive) || p->name.contains("Wireless Controller", Qt::CaseInsensitive));
        if (!keysOk || (!testBit(keys, BTN_SOUTH) && !p->touchpad)) {
            ::close(fd);
            delete p;
            ignored_.insert(path);   // remembered as "not a controller" until it disappears
            return;
        }
        const int axes[6] = {ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ};
        for (int i = 0; i < 6; i++) {
            input_absinfo info{};
            if (ioctl(fd, EVIOCGABS(axes[i]), &info) == 0 && info.maximum > info.minimum) {
                p->absMin[i] = info.minimum;
                p->absMax[i] = info.maximum;
            }
        }
    } else {
        p->name = QStringLiteral("Test controller");
    }
    if (p->name.isEmpty()) p->name = QStringLiteral("Controller");
    p->notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
    connect(p->notifier.get(), &QSocketNotifier::activated, this, [this, path] {
        if (Pad* q = pads_.value(path)) readPad(*q);
    });
    pads_.insert(path, p);
    if (wantGrab_) applyGrab();
    if (!p->touchpad) emit controllersChanged();
}

void GamepadInput::removePad(const QString& path) {
    Pad* p = pads_.take(path);
    if (!p) return;
    if (p->player >= 0) emit playerState(p->player, 0, 0, 0, 0, 0, 0, 0);   // unplugged: that player lets go
    if (p->ffFd >= 0) ::close(p->ffFd);
    ::close(p->fd);
    delete p;
    emit controllersChanged();
}

void GamepadInput::readPad(Pad& p) {
    input_event ev[64];
    for (;;) {
        const ssize_t n = ::read(p.fd, ev, sizeof ev);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
            if (!p.test) QTimer::singleShot(0, this, [this, path = p.path] { removePad(path); });   // unplugged
            return;
        }
        for (size_t i = 0; i < size_t(n) / sizeof(input_event); ++i) {
            const input_event& e = ev[i];
            if (p.touchpad) {   // only the click
                if (e.type == EV_KEY && e.code == BTN_LEFT && e.value != 2) psKey(BTN_LEFT, e.value != 0);
                continue;
            }
            if (e.type == EV_KEY) {
                if (e.value == 2) continue;   // kernel auto-repeat: VeyAmux repeats itself
                psKey(e.code, e.value != 0);   // (the Steam Input copy sends the same state: harmless here)
                const bool dup = duplicate(p, e.code, e.value != 0);
                padKey(p, e.code, e.value != 0, dup);
                if (dup) continue;
                onKey(e.code, e.value != 0);
            } else if (e.type == EV_ABS) {
                if (e.code == ABS_HAT0X || e.code == ABS_HAT0Y) {
                    {   // Remote Play: the D-pad as buttons
                        using namespace remoteplay_bits;
                        const quint32 neg = e.code == ABS_HAT0X ? kLeft : kUp, pos = e.code == ABS_HAT0X ? kRight : kDown;
                        const quint32 before = psButtons_;
                        psButtons_ = (psButtons_ & ~(neg | pos)) | (e.value < 0 ? neg : 0) | (e.value > 0 ? pos : 0);
                        if (psButtons_ != before) emitPad();
                    }
                    const bool dup = duplicate(p, e.code, e.value < 0 ? -1 : e.value > 0 ? 1 : 0);
                    {   // this controller's D-pad
                        using namespace remoteplay_bits;
                        const quint32 neg = e.code == ABS_HAT0X ? kLeft : kUp, pos = e.code == ABS_HAT0X ? kRight : kDown;
                        p.ps = (p.ps & ~(neg | pos)) | (e.value < 0 ? neg : 0) | (e.value > 0 ? pos : 0);
                        if (p.player < 0 && e.value != 0 && !dup) padKey(p, -1, true, false);
                        emitPlayer(p);
                    }
                    if (dup) continue;
                    const bool x = e.code == ABS_HAT0X;
                    const bool neg = e.value < 0, pos = e.value > 0;
                    const int a = x ? 2 : 0;   // up/down = 0/1, left/right = 2/3
                    if (hatDir_[a] != neg) { hatDir_[a] = neg; setDir(x ? "left" : "up", neg); }
                    if (hatDir_[a + 1] != pos) { hatDir_[a + 1] = pos; setDir(x ? "right" : "down", pos); }
                } else if (e.code == ABS_RX || e.code == ABS_RY || e.code == ABS_Z || e.code == ABS_RZ) {
                    const int i = e.code == ABS_RX ? 2 : e.code == ABS_RY ? 3 : e.code == ABS_Z ? 4 : 5;
                    if (i < 4) {
                        const double mid = (p.absMin[i] + p.absMax[i]) / 2.0, half = (p.absMax[i] - p.absMin[i]) / 2.0;
                        axis_[i] = half > 0 ? std::clamp((e.value - mid) / half, -1.0, 1.0) : 0;
                    } else {
                        const double range = p.absMax[i] - p.absMin[i];
                        axis_[i] = range > 0 ? std::clamp((e.value - p.absMin[i]) / range, 0.0, 1.0) : 0;
                    }
                    p.ax[i] = axis_[i];
                    emitPlayer(p);
                    emitAnalog();
                } else if (e.code == ABS_X || e.code == ABS_Y) {
                    const int ax = e.code == ABS_Y;
                    const double mid = (p.absMin[ax] + p.absMax[ax]) / 2.0, half = (p.absMax[ax] - p.absMin[ax]) / 2.0;
                    const double v = half > 0 ? (e.value - mid) / half : 0;
                    axis_[ax] = std::clamp(v, -1.0, 1.0);
                    p.ax[ax] = axis_[ax];
                    emitPlayer(p);
                    emitAnalog();
                    const int a = ax ? 0 : 2;
                    const bool neg = v < -0.6, pos = v > 0.6;
                    if (stickDir_[a] != neg) { stickDir_[a] = neg; setDir(ax ? "up" : "left", neg); }
                    if (stickDir_[a + 1] != pos) { stickDir_[a + 1] = pos; setDir(ax ? "down" : "right", pos); }
                }
            }
        }
    }
}

bool GamepadInput::duplicate(const Pad& p, int key, int value) {
    const qint64 now = clock_.elapsed();
    Seen& s = seen_[key * 4 + value + 1];
    const bool dup = s.ms >= 0 && s.path != p.path && now - s.ms < 60;
    if (!dup) { s.path = p.path; s.ms = now; }
    if (value != 0) {
        // a copy mirrors every press; two players can press together now and then, but not three times in a row
        // with nothing else between (a press of either that the other did not mirror starts the count again)
        Pad& me = const_cast<Pad&>(p);
        if (!dup) {
            if (!me.lastMirrored)
                for (auto it = pairStreak_.begin(); it != pairStreak_.end();)
                    it = it.key().contains(me.path) ? pairStreak_.erase(it) : std::next(it);
            me.lastMirrored = false;
        } else if (Pad* first = pads_.value(s.path)) {
            first->lastMirrored = me.lastMirrored = true;
            const QString pair = first->path < me.path ? first->path + "|" + me.path : me.path + "|" + first->path;
            if (++pairStreak_[pair] >= 3) markTwins(first->path, me.path);
        }
    }
    return dup;
}

// two controllers sent the same press at once: one is a copy of the other (Steam Input's virtual controller, or a
// handheld's driver next to the raw device). The copy is never a player of its own; which one is the copy does not
// depend on whose event came first this time - the one that is already a player stays it. Before this, the copy
// could take player 2 when its event happened to arrive first, and then both sent the same presses.
void GamepadInput::markTwins(const QString& firstPath, const QString& laterPath) {
    Pad* a = pads_.value(firstPath);
    Pad* b = pads_.value(laterPath);
    if (!a || !b || a->twin || b->twin) return;
    Pad* copy = (b->player < 0 || (a->player >= 0 && a->player < b->player)) ? b : a;
    copy->twin = true;
    qInfo("Controller: %s is a copy of %s (not a player of its own)", qPrintable(copy->name),
          qPrintable((copy == a ? b : a)->name));
    if (copy->player >= 0) {
        emit playerState(copy->player, 0, 0, 0, 0, 0, 0, 0);
        copy->player = -1;
    }
}

bool GamepadInput::comboHeld() const { return select_ || start_ || guide_; }

void GamepadInput::onKey(int code, bool down) {
    switch (code) {
    case BTN_SELECT: select_ = down; break;
    case BTN_START: start_ = down; break;
    case BTN_MODE:
        guide_ = down;
        if (down) guideTimer_.start(); else guideTimer_.stop();
        break;
    case BTN_DPAD_UP: setDir("up", down); return;
    case BTN_DPAD_DOWN: setDir("down", down); return;
    case BTN_DPAD_LEFT: setDir("left", down); return;
    case BTN_DPAD_RIGHT: setDir("right", down); return;
    case BTN_SOUTH: if (down && wantGrab_) emit button("a"); return;
    case BTN_EAST: if (down && wantGrab_) emit button("b"); return;
    case BTN_TL: trigBtn_[2] = down; if (down && wantGrab_) emit button("lb"); emitAnalog(); return;
    case BTN_TR: trigBtn_[3] = down; if (down && wantGrab_) emit button("rb"); emitAnalog(); return;
    case BTN_TL2: trigBtn_[0] = down; emitAnalog(); return;
    case BTN_TR2: trigBtn_[1] = down; emitAnalog(); return;
    case BTN_THUMBL: thumbL_ = down; emitAnalog(); return;
    case BTN_NORTH: if (down && !wantGrab_) emit freeButton("y"); return;
    default: return;
    }
    if (select_ && start_ && !comboFired_) { comboFired_ = true; emit menuCombo(); }
    if (!comboHeld()) {
        comboFired_ = false;
        applyGrab();   // a grab or release that waited for the combination to be let go
    }
}

// one controller's own state (players). code -1: only claim a player place (a D-pad press)
void GamepadInput::padKey(Pad& p, int code, bool down, bool dup) {
    using namespace remoteplay_bits;
    quint32 bit = 0;
    switch (code) {
    case BTN_SOUTH: bit = kCross; break;
    case BTN_EAST: bit = kCircle; break;
    case BTN_WEST: bit = kSquare; break;
    case BTN_NORTH: bit = kTriangle; break;
    case BTN_TL: bit = kL1; break;
    case BTN_TR: bit = kR1; break;
    case BTN_THUMBL: bit = kL3; break;
    case BTN_THUMBR: bit = kR3; break;
    case BTN_SELECT: bit = kShare; break;
    case BTN_START: bit = kOptions; break;
    case BTN_DPAD_UP: bit = kUp; break;
    case BTN_DPAD_DOWN: bit = kDown; break;
    case BTN_DPAD_LEFT: bit = kLeft; break;
    case BTN_DPAD_RIGHT: bit = kRight; break;
    case BTN_TL2: p.trig[0] = down; break;
    case BTN_TR2: p.trig[1] = down; break;
    case -1: break;
    default: return;
    }
    if (bit) p.ps = down ? (p.ps | bit) : (p.ps & ~bit);
    // the first press of a controller that has no place yet gives it the next free one (Steam Input's copy is a
    // duplicate and never gets one)
    if (p.player < 0 && down && !dup && !p.twin && !p.touchpad) {
        bool used[4]{};
        for (const Pad* q : pads_) if (q->player >= 0 && q->player < 4) used[q->player] = true;
        for (int i = 0; i < 4; ++i) if (!used[i]) { p.player = i; break; }
        if (p.player >= 0) qInfo("Controller: %s is player %d", qPrintable(p.name), p.player + 1);
    }
    emitPlayer(p);
}

void GamepadInput::emitPlayer(const Pad& p) {
    if (wantGrab_ || p.player < 0) return;
    const double l2 = std::max(p.ax[4], p.trig[0] ? 1.0 : 0.0), r2 = std::max(p.ax[5], p.trig[1] ? 1.0 : 0.0);
    emit playerState(p.player, p.ps, p.ax[0], p.ax[1], p.ax[2], p.ax[3], l2, r2);
}

void GamepadInput::resetPlayers() {
    for (Pad* p : pads_) {
        if (p->player >= 0) emit playerState(p->player, 0, 0, 0, 0, 0, 0, 0);
        p->player = -1;
    }
}

void GamepadInput::psKey(int code, bool down) {
    using namespace remoteplay_bits;
    quint32 bit = 0;
    switch (code) {
    case BTN_SOUTH: bit = kCross; break;
    case BTN_EAST: bit = kCircle; break;
    case BTN_WEST: bit = kSquare; break;
    case BTN_NORTH: bit = kTriangle; break;
    case BTN_TL: bit = kL1; break;
    case BTN_TR: bit = kR1; break;
    case BTN_THUMBL: bit = kL3; break;
    case BTN_THUMBR: bit = kR3; break;
    case BTN_SELECT: bit = kShare; break;
    case BTN_START: bit = kOptions; break;
    case BTN_MODE: bit = kPs; break;
    case BTN_DPAD_UP: bit = kUp; break;
    case BTN_DPAD_DOWN: bit = kDown; break;
    case BTN_DPAD_LEFT: bit = kLeft; break;
    case BTN_DPAD_RIGHT: bit = kRight; break;
    case BTN_LEFT: bit = kTouchpad; break;   // the touchpad device's click
    case BTN_TL2: case BTN_TR2: return;   // digital triggers: onKey -> emitAnalog -> emitPad
    default: return;
    }
    const quint32 before = psButtons_;
    psButtons_ = down ? (psButtons_ | bit) : (psButtons_ & ~bit);
    if (psButtons_ != before) emitPad();
}

void GamepadInput::emitPad() {
    if (wantGrab_) return;
    const double l2 = std::max(axis_[4], trigBtn_[0] ? 1.0 : 0.0);
    const double r2 = std::max(axis_[5], trigBtn_[1] ? 1.0 : 0.0);
    emit padState(psButtons_, axis_[0], axis_[1], axis_[2], axis_[3], l2, r2);
}

void GamepadInput::rumble(int low, int high) {
    for (Pad* p : pads_) {
        if (p->test || p->touchpad) continue;
        if (p->ffFd == -2) {
            p->ffFd = ::open(p->path.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
            if (p->ffFd >= 0) {
                unsigned long ff[(FF_MAX + 1) / (8 * sizeof(long)) + 1]{};
                if (ioctl(p->ffFd, EVIOCGBIT(EV_FF, sizeof ff), ff) < 0 || !testBit(ff, FF_RUMBLE)) { ::close(p->ffFd); p->ffFd = -1; }
            }
        }
        if (p->ffFd < 0) continue;
        ff_effect fx{};
        fx.type = FF_RUMBLE;
        fx.id = int16_t(p->ffId);
        fx.u.rumble.strong_magnitude = uint16_t(std::clamp(low, 0, 255) * 257);
        fx.u.rumble.weak_magnitude = uint16_t(std::clamp(high, 0, 255) * 257);
        fx.replay.length = 3000;   // the game sends a new value when it changes; stops by itself if it does not
        if (ioctl(p->ffFd, EVIOCSFF, &fx) < 0) continue;
        p->ffId = fx.id;
        input_event play{};
        play.type = EV_FF;
        play.code = uint16_t(fx.id);
        play.value = (low > 0 || high > 0) ? 1 : 0;
        if (::write(p->ffFd, &play, sizeof play) < 0) {}
    }
}

void GamepadInput::emitAnalog() {
    emitPad();
    if (wantGrab_) return;   // the menu has the controller
    const double lt = std::max({axis_[4], trigBtn_[0] ? 1.0 : 0.0, trigBtn_[2] ? 1.0 : 0.0});
    const double rt = std::max({axis_[5], trigBtn_[1] ? 1.0 : 0.0, trigBtn_[3] ? 1.0 : 0.0});
    emit analog(axis_[0], axis_[1], axis_[2], axis_[3], lt, rt, thumbL_);
}

void GamepadInput::setDir(const QString& dir, bool down) {
    if (!wantGrab_) { heldDir_.clear(); repeat_.stop(); return; }   // the menu is closed: directions belong to the game
    if (down) {
        heldDir_ = dir;
        emit button(dir);
        repeat_.start(kRepeatDelay);
    } else if (heldDir_ == dir) {
        heldDir_.clear();
        repeat_.stop();
    }
}

void GamepadInput::setGrabbed(bool g) {
    if (g == wantGrab_) return;
    wantGrab_ = g;
    if (!g) { heldDir_.clear(); repeat_.stop(); }
    if (g) emit analog(0, 0, 0, 0, 0, 0, false);   // the demo stops swimming while the menu is open
    if (g) emit padState(0, 0, 0, 0, 0, 0, 0);       // and Remote Play gets an idle controller
    if (g) for (const Pad* p : pads_) if (p->player >= 0) emit playerState(p->player, 0, 0, 0, 0, 0, 0, 0);
    else emitPad();
    applyGrab();
    emit grabbedChanged();
}

void GamepadInput::applyGrab() {
    if (comboHeld()) return;   // wait until the buttons that opened/closed the menu are released
    for (Pad* p : pads_) {
        if (!p || p->test || p->grabbedNow == wantGrab_) continue;
        if (ioctl(p->fd, EVIOCGRAB, wantGrab_ ? 1 : 0) == 0 || !wantGrab_) p->grabbedNow = wantGrab_;
    }
}

} // namespace veyamux
