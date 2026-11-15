// VeyAmux for Linux: adds VeyAmux to Steam as a non-Steam game (so it can be started from Big Picture / Game Mode).
// Steam keeps these in userdata/<account>/config/shortcuts.vdf, a binary KeyValues file. Existing entries are kept;
// an earlier VeyAmux entry is updated instead of added twice. Steam must be closed (it rewrites the file on exit).
#pragma once
#include <QString>

namespace veyamux::steam {

struct Result {
    bool ok = false;
    int accounts = 0;        // Steam accounts the shortcut was written for
    QString message;         // error (English); empty on success
};

bool steamRunning();
// exe: the AppImage; icon: a PNG to show in Steam; launchOptions: arguments for VeyAmux
Result addShortcut(const QString& exe, const QString& icon, const QString& launchOptions);

} // namespace veyamux::steam
