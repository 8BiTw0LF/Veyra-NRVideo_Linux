// VeyAmux for Linux: is variable refresh rate (VRR / Adaptive Sync / FreeSync) switched on for the screens?
//
// lsfg-vk 2 has only one way of pacing its frames: it presents the real and the generated picture right after
// each other and relies on the screen's fixed refresh to space them evenly (its own troubleshooting guide says to
// turn VRR off). With VRR the screen shows them as they come, they bunch up, and Lossless Scaling looks uneven.
// KDE Plasma: `kscreen-doctor -o` ("Vrr: Automatic / Always / Never" per screen; Automatic = on in fullscreen,
// which is how VeyAmux usually runs). Other desktops: unknown (no warning).
#pragma once
#include <QObject>
#include <QString>
#include <functional>

namespace veyamux::display {

enum class Vrr { Unknown, Off, On };
// Asks the desktop in the background; `done` runs on the calling (GUI) thread. `screens`: the names of the screens
// with VRR on (for the message).
void queryVrr(QObject* context, std::function<void(Vrr state, QString screens)> done);
// The text of `kscreen-doctor -o` -> state (exposed for tests)
Vrr parseKscreenDoctor(const QString& text, QString* screens = nullptr);

} // namespace veyamux::display
