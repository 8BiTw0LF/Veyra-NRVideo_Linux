// VeyAmux for Linux: game controllers for the menu.
//
// Controllers are read straight from the kernel (evdev, /dev/input/event*): the Xbox, PlayStation, Switch and generic
// HID drivers all report the same standard codes (BTN_SOUTH = A/Cross, BTN_EAST = B/Circle, BTN_SELECT, BTN_START,
// BTN_MODE = Guide/PS, the D-pad as BTN_DPAD_* or the ABS_HAT0 axes, the left stick as ABS_X/ABS_Y).
//
// The menu opens and closes with Select + Start held together, or with the Guide/PS button held for half a second.
// While the menu is open, every controller is grabbed (EVIOCGRAB), so a game reading the same controller gets nothing.
// The grab starts only after the buttons that opened the menu are released, and ends once the closing buttons are
// released, so the game never sees a button stuck down.
#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QSet>
#include <QObject>
#include <QSocketNotifier>
#include <QStringList>
#include <QTimer>

#include <memory>

namespace veyamux {

class GamepadInput : public QObject {
    Q_OBJECT
    // names of the connected controllers ("Xbox Wireless Controller", ...)
    Q_PROPERTY(QStringList controllers READ controllers NOTIFY controllersChanged)
    // true while the menu is open: the controllers are kept from the game
    Q_PROPERTY(bool grabbed READ grabbed WRITE setGrabbed NOTIFY grabbedChanged)
public:
    explicit GamepadInput(QObject* parent = nullptr);
    ~GamepadInput() override;
    QStringList controllers() const;
    bool grabbed() const { return wantGrab_; }
    void setGrabbed(bool g);
    // the game's rumble (PlayStation Remote Play): low / high frequency motor 0..255, on every controller
    void rumble(int low, int high);

signals:
    void controllersChanged();
    void grabbedChanged();
    // Select + Start, or Guide held: open / close the menu
    void menuCombo();
    // menu navigation (repeats while held): "up", "down", "left", "right", "a", "b", "lb", "rb"
    void button(const QString& name);
    // while the menu is closed (for the demo scene): sticks -1..1, triggers 0..1 (bumpers count as triggers),
    // left stick click; and "y" pressed
    void analog(double lx, double ly, double rx, double ry, double lt, double rt, bool fast);
    void freeButton(const QString& name);
    // while the menu is closed: the whole controller in PlayStation layout (for Remote Play): buttons as
    // remoteplay::Button bits, sticks -1..1, triggers 0..1. All zero when the menu opens.
    void padState(quint32 buttons, double lx, double ly, double rx, double ry, double l2, double r2);
    // the same per controller, for games with several players: a controller becomes player 1, 2, 3 or 4 by its
    // first press (so Steam Input's copy of a controller, whose presses are dropped as duplicates, takes no place)
    void playerState(int player, quint32 buttons, double lx, double ly, double rx, double ry, double l2, double r2);

public slots:
    void resetPlayers();   // a new game: the next presses decide the players again

private:
    struct Pad {
        int fd = -1;
        QString name, path;
        std::unique_ptr<QSocketNotifier> notifier;
        bool test = false;               // VEYAMUX_TEST_PAD_FIFO: raw events from a pipe, no ioctls
        // ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_Z, ABS_RZ
        int absMin[6]{-32768, -32768, -32768, -32768, 0, 0}, absMax[6]{32767, 32767, 32767, 32767, 255, 255};
        bool grabbedNow = false;
        bool touchpad = false;           // a DualSense / DualShock 4 touchpad: only its click is used
        int ffFd = -2;                   // for rumble (-2: not opened yet, -1: not possible)
        int ffId = -1;
        // this controller alone (games with several players)
        int player = -1;
        bool twin = false;               // a copy of another controller (Steam Input's): never a player of its own
        bool lastMirrored = true;        // its last press was sent by another controller too
        quint32 ps = 0;
        double ax[6]{};
        bool trig[2]{};
    };
    void padKey(Pad& p, int code, bool down, bool dup);
    void markTwins(const QString& firstPath, const QString& laterPath);
    void emitPlayer(const Pad& p);
    void scan();
    void addPad(const QString& path, bool test);
    void removePad(const QString& path);
    void readPad(Pad& p);
    void onKey(int code, bool down);
    void setDir(const QString& dir, bool down);
    void applyGrab();
    bool comboHeld() const;
    bool duplicate(const Pad& p, int key, int value);

    // Steam Input (Bazzite, SteamOS) adds a virtual copy of each controller: the same press then arrives from two
    // devices. A press already seen from another device within a few milliseconds is dropped.
    struct Seen { QString path; qint64 ms = -1; };
    QHash<int, Seen> seen_;
    QHash<QString, int> pairStreak_;     // "pathA|pathB": presses in a row both sent              // key: event code * 4 + value (+ 1)
    QElapsedTimer clock_;

    QHash<QString, Pad*> pads_;
    QSet<QString> ignored_;              // event devices that are not controllers (keyboards, mice, ...)
    QTimer scanTimer_, repeat_, guideTimer_;
    bool select_ = false, start_ = false, guide_ = false, comboFired_ = false;
    bool wantGrab_ = false;
    QString heldDir_;                    // direction being repeated
    bool stickDir_[4]{};                 // left stick as directions: up, down, left, right
    bool hatDir_[4]{};
    double axis_[6]{};                   // lx, ly, rx, ry, lt, rt (normalised)
    bool trigBtn_[4]{};                  // BTN_TL2, BTN_TR2, BTN_TL, BTN_TR
    bool thumbL_ = false;
    void emitAnalog();
    quint32 psButtons_ = 0;              // remoteplay::Button bits
    void psKey(int code, bool down);
    void emitPad();
};

} // namespace veyamux
