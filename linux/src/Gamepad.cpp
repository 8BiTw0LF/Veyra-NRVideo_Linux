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
    for (const Pad* p : pads_) n << p->name;
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
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) < 0 || !testBit(keys, BTN_SOUTH)) {
            ::close(fd);
            delete p;
            ignored_.insert(path);   // remembered as "not a controller" until it disappears
            return;
        }
        char name[128]{};
        ioctl(fd, EVIOCGNAME(sizeof name - 1), name);
        p->name = QString::fromUtf8(name).trimmed();
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
    emit controllersChanged();
}

void GamepadInput::removePad(const QString& path) {
    Pad* p = pads_.take(path);
    if (!p) return;
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
            if (e.type == EV_KEY) {
                if (e.value == 2) continue;   // kernel auto-repeat: VeyAmux repeats itself
                if (duplicate(p, e.code, e.value != 0)) continue;
                onKey(e.code, e.value != 0);
            } else if (e.type == EV_ABS) {
                if (e.code == ABS_HAT0X || e.code == ABS_HAT0Y) {
                    if (duplicate(p, e.code, e.value < 0 ? -1 : e.value > 0 ? 1 : 0)) continue;
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
                    emitAnalog();
                } else if (e.code == ABS_X || e.code == ABS_Y) {
                    const int ax = e.code == ABS_Y;
                    const double mid = (p.absMin[ax] + p.absMax[ax]) / 2.0, half = (p.absMax[ax] - p.absMin[ax]) / 2.0;
                    const double v = half > 0 ? (e.value - mid) / half : 0;
                    axis_[ax] = std::clamp(v, -1.0, 1.0);
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
    return dup;
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

void GamepadInput::emitAnalog() {
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
