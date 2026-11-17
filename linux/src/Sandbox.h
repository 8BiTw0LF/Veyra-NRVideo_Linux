// VeyAmux for Linux: how VeyAmux was installed (AppImage or a plain binary) and how the desktop, Steam and the
// autostart entry should start it again.
#pragma once
#include <QCoreApplication>
#include <QString>
#include <QStringList>

namespace veyamux::sandbox {

// The program and leading arguments that start VeyAmux from outside (Steam shortcut, ~/.config/autostart):
// the AppImage file, or the binary itself.
struct Launcher {
    QString program;
    QStringList args;
};
inline Launcher launcher() {
    if (qEnvironmentVariableIsSet("APPIMAGE")) return {qEnvironmentVariable("APPIMAGE"), {}};
    return {QCoreApplication::applicationFilePath(), {}};
}

} // namespace veyamux::sandbox
