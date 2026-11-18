// VeyAmux for Linux: adds VeyAmux to Steam as a non-Steam game (so it can be started from Big Picture / Game Mode).
// Steam keeps these in userdata/<account>/config/shortcuts.vdf, a binary KeyValues file. Existing entries are kept;
// an earlier VeyAmux entry is updated instead of added twice. Steam must be closed (it rewrites the file on exit), so
// the caller closes Steam first (shutdownSteam), writes the shortcut and the library artwork, and starts Steam again.
#pragma once
#include <QImage>
#include <QMap>
#include <QString>

namespace veyamux::steam {

struct Result {
    bool ok = false;
    int accounts = 0;        // Steam accounts the shortcut was written for
    QString message;         // error (English); empty on success
};

bool steamRunning();
// true when VeyAmux itself was started by Steam (Game Mode, or a library entry): closing Steam would close VeyAmux too
bool startedBySteam();
// asks the running Steam to close (steam -shutdown; the Flatpak Steam through flatpak run). false: no way to reach it
bool shutdownSteam();
// starts Steam again, detached and without VeyAmux's own environment (AppImage, Qt and lsfg-vk variables)
bool startSteam();

// Library artwork, keyed by Steam's file name suffix in userdata/<account>/config/grid/<appid><suffix>.png:
// "p" = portrait cover (600x900), "" = wide cover (920x430), "_hero" = background (1920x620), "_logo" = logo over the
// background (transparent). Drawn from the VeyAmux logo (no image files shipped).
QMap<QString, QImage> artwork();

// exe: the AppImage; icon: a PNG to show in Steam; launchOptions: arguments for VeyAmux; grid: artwork() or empty
Result addShortcut(const QString& exe, const QString& icon, const QString& launchOptions,
                   const QMap<QString, QImage>& grid = {});

} // namespace veyamux::steam
