// VeyAmux for Linux: how VeyAmux was installed (AppImage, Flatpak or a plain binary) and how the desktop, Steam and the
// autostart entry should start it again.
#pragma once
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace veyamux::sandbox {

// running inside a Flatpak (Flathub build)
inline bool flatpak() { return QFileInfo::exists("/.flatpak-info") && qEnvironmentVariableIsSet("FLATPAK_ID"); }
inline QString flatpakId() { return qEnvironmentVariable("FLATPAK_ID"); }

// The program and leading arguments that start VeyAmux from outside (Steam shortcut, ~/.config/autostart):
// the AppImage file, `flatpak run <id>`, or the binary itself.
struct Launcher {
    QString program;
    QStringList args;
};
inline Launcher launcher() {
    if (flatpak()) return {QStringLiteral("/usr/bin/flatpak"), {QStringLiteral("run"), QStringLiteral("--command=veyamux"), flatpakId()}};
    if (qEnvironmentVariableIsSet("APPIMAGE")) return {qEnvironmentVariable("APPIMAGE"), {}};
    return {QCoreApplication::applicationFilePath(), {}};
}

// The host's ~/.config (a Flatpak's own config folder lives in ~/.var/app/<id>/config; the autostart entry must go to
// the real one, which the Flatpak is given access to with --filesystem=xdg-config/autostart:create).
inline QString hostConfigDir() { return QDir::homePath() + "/.config"; }

} // namespace veyamux::sandbox
