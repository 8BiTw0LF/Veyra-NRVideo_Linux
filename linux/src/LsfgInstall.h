// VeyAmux for Linux: installs lsfg-vk 2 when only an older lsfg-vk (1.x) is found.
//
// lsfg-vk is not shipped with VeyAmux (CC BY-NC-ND 4.0). On the user's computer, VeyAmux downloads the official build
// from builds.lsfg-vk.dev, checks it against the SHA-256 below, and unpacks it into ~/.local like lsfg-vk's install
// guide does (tar -xf lsfg-vk-<version>.tar.xz -C ~/.local). Only archive entries under bin/, lib/ and share/ are
// accepted. lsfg-vk 1 in the home folder is moved aside first (its layer manifest and library), because the two
// layers must not be loaded together; a system-wide lsfg-vk 1 (a package) is left alone and reported.
#pragma once
#include <QString>
#include <functional>

namespace lsfg {

constexpr const char* kV2Version = "2.0.0";

struct InstallResult {
    bool ok = false;
    QString message;      // English; on failure what went wrong, on success what was done
    QString backupDir;    // where lsfg-vk 1 was moved (empty: nothing moved)
};

// Blocking (network, tar); run it on a worker thread. progress: 0..100 while downloading, called from that thread.
InstallResult installV2(const std::function<void(int percent)>& progress);

// A system-wide lsfg-vk 1 manifest (outside the home folder) that blocks the install, or empty.
QString systemOldLayer();

} // namespace lsfg
