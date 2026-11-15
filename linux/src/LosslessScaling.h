// VeyAmux for Linux - Lossless Scaling frame generation through the separately installed lsfg-vk Vulkan layer.
//
// VeyAmux does not contain, copy or reimplement any lsfg-vk or Lossless Scaling code (lsfg-vk is CC BY-NC-ND 4.0 and
// needs the user's own Steam copy of Lossless Scaling). It only:
//   * finds the user's Lossless Scaling install in any Steam library ($HOME based, so every user name works),
//   * finds an installed lsfg-vk layer,
//   * writes a VeyAmux-only lsfg-vk profile (its own config file, never the user's own lsfg-vk config) and points the
//     layer at it through lsfg-vk's documented environment variables (LSFGVK_CONFIG / LSFGVK_PROFILE).
// lsfg-vk then generates frames on VeyAmux's Vulkan window like it does for games.
#pragma once
#include <QString>
#include <QStringList>

namespace lsfg {

struct Detection {
    QString dllPath;          // lsfg-vk.dll (what lsfg-vk 2.x loads) in the Lossless Scaling folder
    QString losslessDll;      // Lossless.dll, if present (lsfg-vk 1.x used it)
    QString installDir;       // ".../steamapps/common/Lossless Scaling"
    QString layerManifest;    // the lsfg-vk Vulkan layer manifest that was found
    bool layerV2 = false;     // lsfg-vk 2.x (lsfg-vk-cli present / v2 manifest)
    QStringList searched;     // Lossless Scaling folders that were checked (for the UI)
    bool installed() const { return !installDir.isEmpty(); }
    bool ready() const { return !dllPath.isEmpty() && !layerManifest.isEmpty(); }
};

// override: a Lossless Scaling folder or dll chosen by the user (empty = automatic).
Detection detect(const QString& overridePath = {});



// Path of VeyAmux's own lsfg-vk config file (~/.config/VeyAmux/lsfg-vk.toml).
QString configPath();
// lsfg-vk writes its log here (log_file in VeyAmux's config); removed before every start with lsfg-vk.
QString logPath();
// The most telling line of that log (newest error/warning, else the profile line, else the last line). loaded = the
// layer wrote anything at all, i.e. it was loaded into this process.
QString logSummary(bool* loaded);
// Writes/updates VeyAmux's lsfg-vk profile; lsfg-vk hot-reloads the multiplier while running.
// flowScale: lsfg-vk's flow resolution (Lossless Scaling recommends 50 % at 4K, 75 % at 1440p, 100 % at 1080p).
bool writeConfig(const Detection& d, int multiplier, bool performanceMode, double flowScale, QString* error = nullptr);
double recommendedFlowScale(int outputHeight);

// Called before the Vulkan instance exists: enables lsfg-vk for this process with VeyAmux's profile, or keeps any
// installed lsfg-vk away from VeyAmux when Lossless Scaling is not selected.
void configureEnvironment(bool enable);

constexpr const char* kProfileName = "VeyAmux";

} // namespace lsfg
