// VeyAmux for Linux: the FSR 4 add-on (download, check, install).
//
// AMD's FSR 4 model and shaders (MIT) are not part of VeyAmux: they are built for Vulkan in the separate
// VeyAmux-FSR4 repository and published there as one archive. When the user picks FSR 4, VeyAmux downloads that
// archive from GitHub, checks it against the SHA-256 below (a new add-on means a new VeyAmux with the new pair),
// accepts only entries under fsr4/, checks every file against the add-on's manifest and puts it in
// ~/.local/share/VeyAmux/fsr4/<version>. VEYAMUX_FSR4_DIR points at another folder (tests, own builds).
#pragma once
#include <QString>
#include <functional>

namespace fsr4addon {

constexpr const char* kVersion = "2";   // 2: with AMD's FP8 network for the matrix units

// The installed add-on's folder, or empty when it is not there (or does not match its manifest)
QString installedDir();
// Is there a published add-on to download (this VeyAmux knows its checksum)?
bool available();

struct InstallResult { bool ok = false; QString message; };
// Blocking (network, tar); run it on a worker thread. progress 0..100 while downloading (from that thread).
InstallResult install(const std::function<void(int)>& progress);

} // namespace fsr4addon
