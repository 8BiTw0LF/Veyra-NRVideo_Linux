// VeyAmux for Linux: start VeyAmux when the capture card is plugged in.
// `veyamux --watch` runs without a window (from ~/.config/autostart): it watches /dev for new video devices and, when
// the card VeyAmux used last appears, starts VeyAmux fullscreen on it - unless VeyAmux is already running.
#pragma once
#include <QString>

namespace veyamux::watch {

int run(const QString& self);                               // the --watch loop (does not return normally)
QString cardName(const QString& devicePath);                // V4L2 card name from sysfs ("Live Gamer Ultra 2.1")
QString findDeviceByName(const QString& name);              // /dev/videoN of the first capture node with that name
bool autostartEnabled();
bool setAutostart(bool on, const QString& self, QString& error);   // writes/removes the autostart entry, starts/stops the watcher

} // namespace veyamux::watch
