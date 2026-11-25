// VeyAmux for Linux: VeyAmux Debug's tools - the full report, the benchmark and the debug switches.
//
// The report is one Markdown file with everything a bug needs across hardware: the machine, the graphics driver, the
// displays, the source and capture card, sound, Lossless Scaling, frame timing, the benchmark (if run), the settings
// (secrets hidden), the log and earlier crashes. It is written to ~/Documents/VeyAmux reports (or the home folder).
//
// The benchmark runs the live source through a fixed list of settings for a few seconds each and measures the GPU
// time of every stage, frame rate, jitter, delay, CPU use and memory. It answers "what does each setting cost on this
// machine" (MarkStokes-outlook/VeyAmux#13) and is written as a CSV next to the reports.
#include "PlayerBridge.h"
#include "CaptureSource.h"
#include "Diagnostics.h"
#include "LosslessScaling.h"

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cmath>
#include <unistd.h>
#include <vulkan/vulkan.h>
#include <linux/videodev2.h>

using veyamux::EnhanceSettings;
namespace diag = veyamux::diag;

namespace {

double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

QString reportDir() {
    QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (docs.isEmpty()) docs = QDir::homePath();
    const QString d = docs + "/VeyAmux reports";
    QDir().mkpath(d);
    return d;
}

long long cpuTicks() {   // this process's user + system time, in clock ticks
    QFile f("/proc/self/stat");
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QString s = QString::fromLatin1(f.readAll());
    const QStringList p = s.mid(s.lastIndexOf(')') + 2).split(' ');   // fields from 3 on
    return p.size() > 13 ? p[11].toLongLong() + p[12].toLongLong() : 0;
}

double rssMb() {
    QFile f("/proc/self/statm");
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QStringList p = QString::fromLatin1(f.readAll()).split(' ');
    return p.size() > 1 ? p[1].toDouble() * double(sysconf(_SC_PAGESIZE)) / 1048576.0 : 0;
}

struct Spread { double mean = 0, sd = 0, p50 = 0, p99 = 0, max = 0; };
Spread spread(std::vector<double> v) {
    Spread r;
    if (v.empty()) return r;
    for (double x : v) r.mean += x;
    r.mean /= double(v.size());
    for (double x : v) r.sd += (x - r.mean) * (x - r.mean);
    r.sd = std::sqrt(r.sd / double(v.size()));
    std::sort(v.begin(), v.end());
    r.p50 = v[v.size() / 2];
    r.p99 = v[std::min(v.size() - 1, size_t(double(v.size()) * 0.99))];
    r.max = v.back();
    return r;
}

QString vulkanDriver(veyamux::VkContext* ctx) {
    if (!ctx || !ctx->physical()) return "unknown";
    VkPhysicalDeviceDriverProperties drv{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    if (ctx->apiVersion() >= VK_API_VERSION_1_2) p2.pNext = &drv;
    vkGetPhysicalDeviceProperties2(ctx->physical(), &p2);
    const auto& p = p2.properties;
    const char* types[] = {"other", "integrated", "discrete", "virtual", "CPU"};
    QString s = QStringLiteral("%1 (%2, Vulkan %3.%4.%5, vendor 0x%6 device 0x%7)")
                    .arg(QString::fromUtf8(p.deviceName)).arg(p.deviceType <= 4 ? types[p.deviceType] : "?")
                    .arg(VK_API_VERSION_MAJOR(p.apiVersion)).arg(VK_API_VERSION_MINOR(p.apiVersion)).arg(VK_API_VERSION_PATCH(p.apiVersion))
                    .arg(p.vendorID, 4, 16, QChar('0')).arg(p.deviceID, 4, 16, QChar('0'));
    if (p2.pNext) s += QStringLiteral(" · driver %1 %2").arg(QString::fromUtf8(drv.driverName), QString::fromUtf8(drv.driverInfo));
    return s;
}

// switches for VeyAmux Debug, applied as environment variables at the next start (main.cpp)
struct Switch { const char* env; const char* value; const char* title; const char* detail; };
const Switch kSwitches[] = {
    {"VEYAMUX_DEBUG_PACE", "1", "Log every picture's timing",
     "capture → pacer → renderer → drawing, in ms, into the log (the report summarises it either way)"},
    {"VEYAMUX_QT_UPDATE", "1", "Draw through Qt's update() (before 1.8)",
     "waits for the compositor's frame callback; to compare jitter with the 1.8 way"},
    {"VEYAMUX_CAPTURE_COPY", "1", "Always copy capture pictures", "instead of reading them straight from the card's buffers"},
    {"VEYAMUX_DEDUP_SPARSE", "1", "Older repeat detection", "compares a sparse grid of pixels instead of the whole picture"},
    {"VEYAMUX_DEBUG_FG", "1", "Log frame generation", "FSR frame generation: every decision, into the log"},
    {"VEYAMUX_NO_FIT", "1", "No fit-to-window resampling", "the finished picture is stretched by Qt instead"},
    {"VEYAMUX_FILE_GPU", "0", "Video files on the CPU path", "instead of the GPU (live) path"},
    {"VEYAMUX_PS_SWDECODE", "1", "Remote Play: decode on the CPU", "instead of VA-API"},
    {"VEYAMUX_GAMESCOPE_KEEPALIVE", "0", "gamescope: no pointer keep-alive", "to test whether frame generation stops without it"},
    {"VEYAMUX_VSYNC", "1", "VSync on", "the UI waits for the display (VeyAmux runs without it by default)"},
};

} // namespace

bool PlayerBridge::debugBuild() const { return diag::kDebugBuild; }

QString PlayerBridge::buildInfo() const {
    return QStringLiteral("VeyAmux%1 %2 · %3").arg(diag::kDebugBuild ? " Debug" : "", appVersion(), diag::gitSha());
}

void PlayerBridge::setGpuCounters(bool on) {
    if (!liveVk_) return;
    gpuCounters_ = on && liveVk_->setCountersEnabled(on);
    if (!on) liveVk_->setCountersEnabled(false);
    liveVk_->resetGpuSums();
    emit benchChanged();
}

QString PlayerBridge::gpuCounterText() const {
    if (!liveVk_ || !gpuCounters_) return {};
    const auto v = liveVk_->counterValues();
    if (v.empty() || v.front().frames == 0) return L("No frames counted yet.", "Ingen billeder talt endnu.");
    QStringList r;
    r << QStringLiteral("%1 frames:").arg(v.front().frames);
    for (const auto& c : v)
        r << QStringLiteral("%1: %2 %3%4").arg(QString::fromStdString(c.name)).arg(c.avg, 0, 'g', 5).arg(QString::fromStdString(c.unit))
                 .arg(c.avg == 0 && c.frames >= 100 ? QStringLiteral(" (always 0: the driver does not count this one here)") : QString());
    return r.join('\n');
}

QVariantList PlayerBridge::debugSwitches() const {
    QVariantList out;
    QSettings q;
    for (const auto& s : kSwitches) {
        QVariantMap m;
        m["env"] = s.env;
        m["title"] = s.title;
        m["detail"] = QString(s.detail) + "  (" + s.env + "=" + s.value + ")";
        m["on"] = q.contains(QString("debug/env/") + s.env);
        out << m;
    }
    return out;
}

void PlayerBridge::setDebugSwitch(const QString& env, bool on) {
    QSettings q;
    for (const auto& s : kSwitches)
        if (env == s.env) {
            if (on) q.setValue("debug/env/" + env, QString(s.value));   // main.cpp sets it at the next start
            else q.remove("debug/env/" + env);
        }
    debugRestart_ = true;
    emit benchChanged();
}

QString PlayerBridge::paceSummary() {
    if (!liveVk_) return "No live path.";
    const auto ps = liveVk_->paceSamples();
    if (ps.size() < 10) return "Not enough pictures drawn yet.";
    std::vector<double> due, handed, draw, total, gap;
    for (const auto& p : ps) {
        due.push_back(p.due); handed.push_back(p.handed); draw.push_back(p.draw); total.push_back(p.total);
        if (p.gap > 0) gap.push_back(p.gap);
    }
    QStringList r;
    r << QStringLiteral("Last %1 pictures, ms:").arg(ps.size()) << ""
      << "| | mean | sd | median | 99 % | max |" << "|---|---:|---:|---:|---:|---:|";
    auto row = [&](const char* name, const std::vector<double>& v) {
        const Spread s = spread(v);
        r << QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 |").arg(name).arg(s.mean, 0, 'f', 2).arg(s.sd, 0, 'f', 2)
                 .arg(s.p50, 0, 'f', 2).arg(s.p99, 0, 'f', 2).arg(s.max, 0, 'f', 2);
    };
    row("capture → pacer due", due);
    row("due → renderer (or capture → renderer)", handed);
    row("renderer → drawing starts", draw);
    row("capture → drawing (total)", total);
    row("gap between drawings", gap);
    r << "" << QStringLiteral("Pacing repair: %1 · hold %2 ms · share repaired %3 %")
                   .arg(s_.pacingRepair ? "on" : "off").arg(engine_.pacingHoldMs(), 0, 'f', 1)
                   .arg(std::max(0.0, engine_.pacingRepairShare()) * 100.0, 0, 'f', 0);
    return r.join('\n');
}

QString PlayerBridge::saveReport(const QString& note) {
    updateOverlay();
    QString md;
    QTextStream o(&md);
    const QDateTime now = QDateTime::currentDateTime();
    o << "# VeyAmux report\n\n";
    o << "- **Build:** " << buildInfo() << "\n";
    o << "- **Time:** " << now.toString(Qt::ISODate) << " · language " << lang_ << "\n";
    if (!note.trimmed().isEmpty()) o << "\n## What happened\n\n" << note.trimmed() << "\n";

    o << "\n## System\n\n```\n" << diag::systemInfo() << "\n```\n";

    o << "\n## Graphics\n\n";
    veyamux::VkContext* ctx = liveVk_ ? liveVk_->context() : nullptr;
    o << "- **Vulkan device used by VeyAmux:** " << vulkanDriver(ctx) << "\n";
    o << "- **Live path:** " << (liveVulkanActive() ? "active" : "not active") << "\n";
    QString vki = diag::tool("vulkaninfo", {"--summary"}, 6000, 400);
    if (const int a = vki.indexOf("Instance Extensions"), b = vki.indexOf("Instance Layers"); a >= 0 && b > a) vki.remove(a, b - a);
    o << "\n<details><summary>vulkaninfo --summary</summary>\n\n```\n" << vki
      << "\n```\n</details>\n";
    o << "\n<details><summary>vainfo (video decode / encode on the GPU)</summary>\n\n```\n" << diag::tool("vainfo", {}, 6000, 80)
      << "\n```\n</details>\n";

    o << "\n## Displays\n\n";
    for (QScreen* sc : QGuiApplication::screens())
        o << "- " << sc->name() << " · " << sc->manufacturer() << " " << sc->model() << " · " << sc->size().width() << "×"
          << sc->size().height() << " @ " << QString::number(sc->refreshRate(), 'f', 2) << " Hz · scale "
          << sc->devicePixelRatio() << (sc == QGuiApplication::primaryScreen() ? " · primary" : "") << "\n";
    o << "- VRR on the desktop: " << (desktopVrr_ ? "on" : "off") << (vrrScreens_.isEmpty() ? "" : " (" + vrrScreens_ + ")") << "\n";
    o << "- HDR output: " << (hdrOutputActive() ? "on" : "off") << "\n";

    o << "\n## Source and settings\n\n```\n" << perfReport() << "\n```\n";

    o << "\n## Capture cards\n\n";
    const auto devices = veyamux::listCaptureDevices();
    if (devices.empty()) o << "None found.\n";
    for (const auto& d : devices) {
        o << "- **" << QString::fromStdString(d.name) << "** (" << QString::fromStdString(d.path) << ")";
        if (engine_.isLive() && QString::fromStdString(d.path) == curDevice_) o << " · in use";
        o << "\n";
        int n = 0;
        for (const auto& m : veyamux::listCaptureModes(d.path)) {
            if (++n > 40) { o << "  - …\n"; break; }
            o << "  - " << QString::fromStdString(m.label) << "\n";
        }
    }

    o << "\n## Frame timing\n\n" << paceSummary() << "\n";
    if (gpuCounters_) o << "\n## GPU hardware counters\n\n```\n" << gpuCounterText() << "\n```\n";

    if (!bench_.rows.empty()) o << "\n## Benchmark\n\n" << benchTable() << "\n";

    o << "\n## Sound\n\n```\n" << diag::tool("pactl", {"info"}, 3000, 30) << "\n```\n";

    o << "\n## Lossless Scaling\n\n";
    o << "- State: " << lsfgState() << "\n";
    o << "- Lossless Scaling folder: " << (lsfg_.installDir.isEmpty() ? "not found" : lsfg_.installDir) << "\n";
    o << "- lsfg-vk.dll: " << (lsfg_.dllPath.isEmpty() ? "not found" : lsfg_.dllPath) << " · Lossless.dll: "
      << (lsfg_.losslessDll.isEmpty() ? "not found" : lsfg_.losslessDll) << "\n";
    o << "- Layer: " << (lsfg_.layerManifest.isEmpty() ? "not found" : lsfg_.layerManifest) << (lsfg_.layerV2 ? " (lsfg-vk 2)" : "") << "\n";
    {
        bool loaded = false;
        const QString sum = lsfg::logSummary(&loaded);
        o << "- lsfg-vk log: " << (loaded ? sum : QStringLiteral("not loaded in this run")) << "\n";
        const QString cfg = diag::readText(lsfg::configPath(), 8192);
        if (!cfg.isEmpty()) o << "\n<details><summary>VeyAmux's lsfg-vk profile</summary>\n\n```toml\n" << cfg << "\n```\n</details>\n";
    }

    o << "\n## All settings\n\n<details><summary>" << QSettings().fileName() << " (secrets hidden)</summary>\n\n```\n";
    {
        QSettings q;
        for (const QString& k : q.allKeys())
            o << k << " = " << (diag::secretKey(k) ? QStringLiteral("(hidden)") : q.value(k).toString()) << "\n";
    }
    o << "```\n</details>\n";

    const QStringList crashes = diag::crashFiles();
    if (!crashes.isEmpty()) {
        o << "\n## Crashes\n\n";
        for (int i = 0; i < std::min<int>(3, crashes.size()); ++i)
            o << "### " << QFileInfo(crashes[i]).fileName() << " (" << QFileInfo(crashes[i]).lastModified().toString(Qt::ISODate)
              << ")\n\n```\n" << diag::readText(crashes[i], 32 * 1024) << "\n```\n";
    }

    o << "\n## Log (newest " << 500 << " lines)\n\n<details><summary>log</summary>\n\n```\n" << diag::recentLog(500).join('\n')
      << "\n```\n</details>\n";
    if (!diag::logFile().isEmpty()) o << "\nFull log of this run: `" << diag::logFile() << "`\n";
    o.flush();

    const QString path = reportDir() + "/veyamux-report-" + now.toString("yyyyMMdd-HHmmss") + ".md";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(md.toUtf8()) < 0) {
        showNotice(L("Could not write the report", "Kunne ikke skrive rapporten"));
        return {};
    }
    f.close();
    lastReport_ = path;
    emit benchChanged();
    showNotice(L("Report saved: %1", "Rapport gemt: %1").arg(QFileInfo(path).fileName()));
    return path;
}

void PlayerBridge::openReportFolder() { QDesktopServices::openUrl(QUrl::fromLocalFile(reportDir())); }

void PlayerBridge::copyReportPath() {
    if (lastReport_.isEmpty()) return;
    QGuiApplication::clipboard()->setText(lastReport_);
    showNotice(L("Path copied", "Sti kopieret"));
}

void PlayerBridge::reportOnGitHub(const QString& note) {
    if (lastReport_.isEmpty()) saveReport(note);
    QString body = "**What happened**\n\n" + (note.trimmed().isEmpty() ? QStringLiteral("(describe it here)") : note.trimmed()) +
                   "\n\n**Report**\n\nDrag the report file here: `" + QFileInfo(lastReport_).fileName() + "`\n\n" + buildInfo();
    if (liveVk_ && liveVk_->context()) body += " · " + vulkanDriver(liveVk_->context()).section(" (", 0, 0);
    QUrlQuery q;
    q.addQueryItem("title", "[" + appVersion() + "] ");
    q.addQueryItem("body", body);
    QUrl u("https://github.com/8BiTw0LF/VeyAmux/issues/new");
    u.setQuery(q);
    QDesktopServices::openUrl(u);
    openReportFolder();
}

// ---------------------------------------------------------------------------------------------------------------
// Benchmark
//
// 1. Prepare: every step's settings for a moment, so all shaders and images exist before anything is measured (the
//    first use of a pass builds its pipeline, a hitch that used to land in a measurement).
// 2. Each step: settle, then measure: frame times of the drawn pictures, unsmoothed GPU time per stage, delay, CPU per
//    thread and memory. A step whose frame rate or jitter is off from the plain picture's is measured again once; if
//    it stays off it is flagged (then it is a real result: the GPU or the pacing did not keep up).
// 3. On a 4K capture card, the upscaling steps run on the card's 1080p mode (same rate), then the card goes back.

namespace {
std::map<int, std::pair<QString, long long>> threadTicks() {
    std::map<int, std::pair<QString, long long>> out;
    QDir d("/proc/self/task");
    for (const QString& tid : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile f(d.filePath(tid) + "/stat");
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QString st = QString::fromLatin1(f.readAll());
        const int l = st.indexOf('('), r = st.lastIndexOf(')');
        if (l < 0 || r < l) continue;
        const QStringList p = st.mid(r + 2).split(' ');
        if (p.size() > 13) out[tid.toInt()] = {st.mid(l + 1, r - l - 1), p[11].toLongLong() + p[12].toLongLong()};
    }
    return out;
}
} // namespace

void PlayerBridge::startBenchmark() {
    if (bench_.running || sig_.running) return;
    if (!hasMedia() || !liveVulkanActive()) {
        bench_.text = L("Start a capture card or the demo first: the benchmark measures the live picture.",
                        "Start et capture-kort eller demoen først: benchmarken måler det levende billede.");
        emit benchChanged();
        return;
    }
    bench_ = Bench{};
    bench_.saved = s_;
    bench_.source = title();
    bench_.savedMode = curMode_;
    bench_.device = curDevice_;
    bench_.audio = curAudio_;
    const int srcH = engine_.info().height > 0 ? engine_.info().height : 1080;
    const bool card = curMode_.fourcc != 0;
    const bool lsfgOn = s_.lsfgFg() || lsfgActive_;
    const bool fgOn = s_.frameGen != veyamux::FrameGenMode::Off;

    // a 4K card: its uncompressed 1080p mode at the same rate, for the upscaling steps
    bool lowRes = false;
    if (srcH > 1440 && card) {
        double best = 1e9;
        for (const auto& m : veyamux::listCaptureModes(curDevice_.toStdString())) {
            // exactly 1920×1080 (not an ultrawide 2560×1080), uncompressed SDR
            if (m.w != 1920 || m.h != 1080 || m.fourcc == 0 || m.fourcc == V4L2_PIX_FMT_MJPEG || m.fourcc == V4L2_PIX_FMT_P010) continue;
            const double d = std::abs(m.fps - curMode_.fps) + (m.fourcc == V4L2_PIX_FMT_NV12 ? 0 : 0.1);
            if (d < best) { best = d; bench_.lowMode = m; lowRes = true; }
        }
        if (!lowRes) bench_.notes << L("Upscaling left out: the card has no uncompressed 1080p mode, and 2× of 4K would be 8K.",
                                       "Opskalering udeladt: kortet har ingen ukomprimeret 1080p-tilstand, og 2× af 4K ville give 8K.");
    } else if (srcH > 1440) {
        bench_.notes << L("Upscaling left out: the source is 4K (2× would be 8K). Use a 1080p source to include it.",
                          "Opskalering udeladt: kilden er 4K (2× ville give 8K). Brug en 1080p-kilde for at få den med.");
    }
    const bool upNative = srcH <= 1440;

    auto base = [](EnhanceSettings& e) {
        e.enabled = true; e.scale = 1.0f; e.upscaler = veyamux::Upscaler::Sgsr; e.fxaa = false; e.smaa = false;
        e.deflicker = false; e.denoise = veyamux::Denoise::Off; e.sharpen = 0; e.texture = e.clarity = e.dehaze = 0;
        e.retroTv = false; e.pixelPerfect = false; e.hudLock = false;
        if (e.frameGen == veyamux::FrameGenMode::FsrFree) e.frameGen = veyamux::FrameGenMode::Off;
    };
    auto add = [&](const QString& section, const QString& name, std::function<void(EnhanceSettings&)> f, bool low = false, bool more = false) {
        bench_.steps.push_back({section, name, [base, f](EnhanceSettings& e) { base(e); if (f) f(e); }, low, more});
    };
    using veyamux::Denoise;
    using veyamux::Upscaler;
    const QString P = "Picture", N = "Noise reduction", M = "Motion (shared estimate)", U = "Upscaling", G = "Frame generation",
                  T = "Pacing", H = "Combined";
    add(P, "Enhance off", [](EnhanceSettings& e) { e.enabled = false; });
    add(P, "Enhance on, nothing else", nullptr);
    add(P, "Sharpness 0.5", [](EnhanceSettings& e) { e.sharpen = 0.5f; });
    add(P, "Texture, clarity, dehaze", [](EnhanceSettings& e) { e.texture = 0.3f; e.clarity = 0.3f; e.dehaze = 0.2f; });
    add(P, "FXAA high", [](EnhanceSettings& e) { e.fxaa = true; e.fxaaQuality = 1; });
    add(P, "SMAA high", [](EnhanceSettings& e) { e.fxaa = true; e.smaa = true; e.fxaaQuality = 1; });
    add(P, "SMAA ultra", [](EnhanceSettings& e) { e.fxaa = true; e.smaa = true; e.fxaaQuality = 2; });
    add(P, "Retro TV (240 lines)", [](EnhanceSettings& e) { e.retroTv = true; e.retroLines = 240; });
    const char* str[] = {"light", "medium", "strong"};
    const std::pair<Denoise, const char*> nr[] = {{Denoise::Spatial, "spatial"}, {Denoise::Temporal, "temporal"}, {Denoise::Both, "both"}};
    for (const auto& [mode, label] : nr)
        for (int k = 0; k < 3; ++k)
            add(N, QString("%1 %2").arg(label, str[k]), [mode = mode, k](EnhanceSettings& e) { e.denoise = mode; e.denoiseStrength = k; });
    add(M, "Anti-flicker", [](EnhanceSettings& e) { e.deflicker = true; });
    add(M, "Anti-flicker + temporal medium", [](EnhanceSettings& e) { e.deflicker = true; e.denoise = Denoise::Temporal; e.denoiseStrength = 1; });
    add(M, "FSR 3.1 native (TAA)", [](EnhanceSettings& e) { e.upscaler = Upscaler::AmdFsr3; });
    if (fsr4State() == 3) add(M, "FSR 4 native (AA)", [](EnhanceSettings& e) { e.upscaler = Upscaler::AmdFsr4; });
    add(M, "FSR 3.1 native + temporal medium", [](EnhanceSettings& e) { e.upscaler = Upscaler::AmdFsr3; e.denoise = Denoise::Temporal; e.denoiseStrength = 1; });
    if (upNative || lowRes) {
        const bool low = !upNative;
        if (low) add(U, "1080p: enhance on, nothing else", nullptr, true);
        const std::pair<Upscaler, const char*> ups[] = {{Upscaler::Sgsr, "SGSR"}, {Upscaler::AmdFsr1, "FSR 1"}, {Upscaler::Nis, "NIS"}, {Upscaler::AmdFsr3, "FSR 3.1"}};
        for (const auto& [up, label] : ups)
            add(U, QString("%1Upscale 2× %2").arg(low ? "1080p: " : "", label), [up = up](EnhanceSettings& e) { e.scale = 2; e.upscaler = up; }, low);
        add(U, QString("%1Upscale 1.5× FSR 3.1").arg(low ? "1080p: " : ""), [](EnhanceSettings& e) { e.scale = 1.5f; e.upscaler = Upscaler::AmdFsr3; }, low);
        if (fsr4State() == 3) {
            add(U, QString("%1Upscale 2× FSR 4").arg(low ? "1080p: " : ""), [](EnhanceSettings& e) { e.scale = 2; e.upscaler = Upscaler::AmdFsr4; }, low);
            add(U, QString("%1Upscale 1.5× FSR 4").arg(low ? "1080p: " : ""), [](EnhanceSettings& e) { e.scale = 1.5f; e.upscaler = Upscaler::AmdFsr4; }, low);
        }
        add(U, QString("%1Heavy: both medium + anti-flicker + SMAA + 2× FSR 3.1").arg(low ? "1080p: " : ""), [](EnhanceSettings& e) {
            e.denoise = Denoise::Both; e.denoiseStrength = 1; e.deflicker = true; e.fxaa = true; e.smaa = true;
            e.upscaler = Upscaler::AmdFsr3; e.scale = 2;
        }, low);
    }
    if (!lsfgOn) {
        add(G, "FSR 3.1.6 frame generation", [](EnhanceSettings& e) { e.frameGen = veyamux::FrameGenMode::FsrFree; }, false, true);
        add(G, "FSR 3.1.6 frame generation + HUD lock", [](EnhanceSettings& e) { e.frameGen = veyamux::FrameGenMode::FsrFree; e.hudLock = true; }, false, true);
    } else {
        bench_.notes << L("FSR 3.1.6 frame generation left out: Lossless Scaling is on (it needs a restart to switch). Set frame generation to Off to include it.",
                          "FSR 3.1.6 frame generation udeladt: Lossless Scaling er slået til (skift kræver genstart). Sæt frame generation til Fra for at få den med.");
        bench_.notes << L("Lossless Scaling's own frames are made after VeyAmux (in lsfg-vk) and are not part of these numbers.",
                          "Lossless Scalings egne billeder laves efter VeyAmux (i lsfg-vk) og er ikke med i tallene.");
    }
    if (fgOn) {
        add(T, "Even out frame pacing off", [](EnhanceSettings& e) { e.pacingRepair = false; });
        add(T, "Even out frame pacing on", [](EnhanceSettings& e) { e.pacingRepair = true; });
    } else {
        bench_.notes << L("Frame pacing left out: it only works with frame generation on.",
                          "Billedrytme udeladt: den virker kun med frame generation slået til.");
    }
    if (lowLatency_ && lowLatency_->available()) {
        const QString m = lowLatency_->method();
        add(T, m + " off", nullptr);
        bench_.steps.back().lowLatency = 0;
        add(T, m + " on", nullptr);
        bench_.steps.back().lowLatency = 1;
        bench_.notes << L("The delay column ends when VeyAmux's picture is finished; the time it then waits for the screen (what Anti-Lag and Reflex shorten) is not in it.",
                          "Forsinkelseskolonnen slutter, når VeyAmux' billede er færdigt; tiden det derefter venter på skærmen (det Anti-Lag og Reflex forkorter) er ikke med.");
    } else {
        bench_.notes << L("Low latency left out: this GPU/driver has neither AMD Anti-Lag nor NVIDIA Reflex.",
                          "Lav forsinkelse udeladt: grafikkortet/driveren har hverken AMD Anti-Lag eller NVIDIA Reflex.");
    }
    add(H, "Heavy: both medium + anti-flicker + SMAA + FSR 3.1 native", [](EnhanceSettings& e) {
        e.denoise = Denoise::Both; e.denoiseStrength = 1; e.deflicker = true; e.fxaa = true; e.smaa = true; e.upscaler = Upscaler::AmdFsr3;
    });
    // the 1080p steps last, so the card changes mode only once (and back)
    std::stable_partition(bench_.steps.begin(), bench_.steps.end(), [](const BenchStep& st) { return !st.lowRes; });

    bench_.running = true;
    bench_.phase = 0;
    bench_.prep = 0;
    bench_.t0 = nowMs();
    bench_.timer = new QTimer(this);
    connect(bench_.timer, &QTimer::timeout, this, [this] { benchTick(); });
    bench_.timer->start(100);
    qInfo("Benchmark: %d steps on %s", int(bench_.steps.size()), qPrintable(bench_.source));
    for (const QString& n : bench_.notes) qInfo("Benchmark: %s", qPrintable(n));
}

void PlayerBridge::benchApplyStep() {
    const BenchStep& st = bench_.steps[bench_.step];
    EnhanceSettings e = bench_.saved;
    st.set(e);
    s_ = e;
    applyEnhance();
    if (lowLatency_) lowLatency_->setEnabled(st.lowLatency >= 0 ? st.lowLatency == 1 : QSettings().value("disp/lowLatency", false).toBool());
    bench_.phase = 1;
    bench_.t0 = nowMs();
    bench_.cur = BenchRow{};
    bench_.cur.section = st.section;
    bench_.cur.name = st.name;
    bench_.text = L("Benchmark %1/%2: %3%4", "Benchmark %1/%2: %3%4")
                      .arg(bench_.step + 1).arg(bench_.steps.size()).arg(st.name)
                      .arg(bench_.retried ? L(" (again)", " (igen)") : QString());
    emit benchChanged();
}

void PlayerBridge::benchTick() {
    if (!bench_.running) return;
    const double t = nowMs();
    static const bool fast = qEnvironmentVariableIsSet("VEYAMUX_BENCH_FAST");   // tests: much shorter
    const double kPrep = fast ? 150 : 600, kSettle = fast ? 300 : 1500, kMeasure = fast ? 800 : 5000, kRestart = fast ? 1500 : 6000;

    if (bench_.phase == 4) {   // switch the card to its 1080p mode
        if (t - bench_.t0 < 2000) return;
        const EnhanceSettings keep = s_;
        const bool started = startCapture(bench_.device, bench_.lowMode, bench_.audio);
        s_ = keep;
        if (started) { bench_.phase = 3; bench_.t0 = nowMs(); return; }
        if (++bench_.switchTries < 3) { bench_.t0 = nowMs(); return; }
        bench_.notes << L("The upscaling steps were not measured: the card would not start %1 (%2).",
                          "Opskaleringstrinnene blev ikke målt: kortet ville ikke starte %1 (%2).")
                            .arg(QString::fromStdString(bench_.lowMode.label), lastCaptureError_);
        finishBenchmark();
        return;
    }
    if (bench_.phase == 3) {   // the card changes mode: wait until the new mode delivers pictures
        if (t - bench_.t0 < kRestart || !hasMedia() || !liveVulkanActive()) {
            if (t - bench_.t0 > 20000) {
                bench_.notes << L("The upscaling steps were not measured: the card did not deliver pictures in %1.",
                                  "Opskaleringstrinnene blev ikke målt: kortet leverede ikke billeder i %1.")
                                    .arg(QString::fromStdString(bench_.lowMode.label));
                finishBenchmark();
            }
            return;
        }
        bench_.phase = 0;                // prepare the 1080p steps' passes too
        bench_.prep = bench_.step;
        bench_.t0 = t;
        return;
    }
    if (!hasMedia() || !liveVulkanActive()) {
        bench_.notes << L("Stopped: the picture went away.", "Stoppet: billedet forsvandt.");
        finishBenchmark();
        return;
    }

    if (bench_.phase == 0) {   // prepare: every step of the native source briefly
        if (t - bench_.t0 < kPrep && bench_.prep > bench_.step) return;
        while (bench_.prep < bench_.steps.size() && bench_.steps[bench_.prep].lowRes != bench_.onLowRes) ++bench_.prep;
        if (bench_.prep >= bench_.steps.size()) { bench_.retried = false; benchApplyStep(); return; }
        EnhanceSettings e = bench_.saved;
        bench_.steps[bench_.prep].set(e);
        s_ = e;
        applyEnhance();
        bench_.text = L("Benchmark: preparing %1/%2", "Benchmark: forbereder %1/%2").arg(bench_.prep + 1).arg(bench_.steps.size());
        emit benchChanged();
        ++bench_.prep;
        bench_.t0 = t;
        return;
    }
    if (bench_.phase == 1) {
        if (t - bench_.t0 < kSettle) return;
        bench_.phase = 2;
        bench_.t0 = t;
        bench_.cpuTicks0 = cpuTicks();
        bench_.threads0 = threadTicks();
        liveVk_->resetGpuSums();
        return;
    }
    // phase 2: measuring
    auto& r = bench_.cur;
    r.delay += liveVk_->pipelineMs();
    ++r.samples;
    if (t - bench_.t0 < kMeasure) return;
    const double secs = (t - bench_.t0) / 1000.0;
    if (r.samples) r.delay /= r.samples;
    const auto g = liveVk_->gpuSums();
    r.gpuFrames = g.frames;
    if (gpuCounters_) {
        QStringList cs;
        for (const auto& c : liveVk_->counterValues())
            cs << QStringLiteral("%1=%2%3").arg(QString::fromStdString(c.name)).arg(c.avg, 0, 'g', 5)
                      .arg(c.unit.empty() ? QString() : " " + QString::fromStdString(c.unit));
        r.counters = cs.join("; ");
    }
    r.gpu = g.frames ? g.total / g.frames : -1;
    for (size_t k = 0; k < r.stages.size() && k < g.stage.size(); ++k) r.stages[k] = g.ran[k] ? g.stage[k] / g.ran[k] : -1;
    std::vector<double> shown = liveVk_->shownTimes(), iv;
    for (size_t i = 1; i < shown.size(); ++i)
        if (shown[i - 1] >= bench_.t0) iv.push_back(shown[i] - shown[i - 1]);
    if (iv.size() >= 4) {
        const Spread sp = spread(iv);
        r.frameMs = sp.mean; r.fps = 1000.0 / sp.mean; r.jitter = sp.sd; r.p99 = sp.p99;
    }
    const double tck = double(sysconf(_SC_CLK_TCK));
    r.cpu = double(cpuTicks() - bench_.cpuTicks0) / tck / secs * 100.0;
    r.rssMb = rssMb();
    {   // CPU per thread (by name), the busiest first
        std::map<QString, double> byName;
        for (const auto& [tid, now] : threadTicks()) {
            const auto it = bench_.threads0.find(tid);
            const long long d = now.second - (it != bench_.threads0.end() ? it->second.second : 0);
            if (d > 0) byName[now.first] += double(d) / tck / secs * 100.0;
        }
        std::vector<std::pair<double, QString>> v;
        for (const auto& [n, pc] : byName) if (pc >= 0.5) v.push_back({pc, n});
        std::sort(v.rbegin(), v.rend());
        QStringList parts;
        for (size_t i = 0; i < v.size() && i < 5; ++i) parts << QStringLiteral("%1 %2").arg(v[i].second).arg(v[i].first, 0, 'f', 0);
        r.threads = parts.join(", ");
    }
    {
        const auto& in = engine_.info();
        r.source = QStringLiteral("%1×%2").arg(in.width).arg(in.height);
        if (curMode_.fourcc) r.source = QString::fromStdString(curMode_.label);
    }
    // the plain picture sets the reference; a step far from it is measured again once, then flagged
    const BenchStep& st = bench_.steps[bench_.step];
    if (st.name == "Enhance on, nothing else" || st.name == "1080p: enhance on, nothing else") { bench_.refFps = r.fps; bench_.refJitter = r.jitter; }
    bool odd = false;
    if (!st.moreFrames && bench_.refFps > 0 && r.fps > 0)
        odd = std::abs(r.fps - bench_.refFps) / bench_.refFps > 0.03 || r.jitter > std::max(1.0, 4.0 * bench_.refJitter);
    if (r.fps <= 0) odd = true;
    if (odd && !bench_.retried) { bench_.retried = true; benchApplyStep(); return; }
    if (bench_.retried) r.flag = r.fps <= 0 ? QStringLiteral("no pictures") : odd ? QStringLiteral("uneven") : QStringLiteral("re-measured");
    bench_.rows.push_back(r);
    bench_.retried = false;

    if (++bench_.step < bench_.steps.size()) {
        const BenchStep& next = bench_.steps[bench_.step];
        if (next.lowRes && !bench_.onLowRes) {   // switch the card to its 1080p mode once
            bench_.onLowRes = true;
            bench_.text = L("Benchmark: switching the card to %1…", "Benchmark: skifter kortet til %1…").arg(QString::fromStdString(bench_.lowMode.label));
            emit benchChanged();
            bench_.phase = 4;   // switch (tried up to three times, two seconds apart)
            bench_.switchTries = 0;
            bench_.t0 = nowMs() - 10000;
            return;
        }
        benchApplyStep();
        return;
    }
    finishBenchmark();
}

// Writes what was measured (also when it stopped early) and puts the settings and the card's mode back.
void PlayerBridge::finishBenchmark() {
    const bool complete = bench_.step >= bench_.steps.size();
    stopBenchmark(false);
    if (bench_.rows.empty()) { emit benchChanged(); return; }
    const QString path = reportDir() + "/veyamux-benchmark-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".csv";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        QTextStream o(&f);
        o << "# " << buildInfo() << " · " << bench_.source << " · "
          << (liveVk_ && liveVk_->context() ? vulkanDriver(liveVk_->context()) : QString()) << "\n";
        for (const QString& n : bench_.notes) o << "# " << n << "\n";
        o << "section,step,source,fps,frame_ms,jitter_ms,p99_ms,delay_ms,gpu_ms,gpu_input,gpu_denoise,gpu_aa,gpu_upscale,gpu_finish,gpu_output,gpu_framegen,"
             "gpu_frames,cpu_pct,rss_mb,threads,flag,counters\n";
        for (const auto& b : bench_.rows) {
            o << '"' << b.section << "\",\"" << b.name << "\",\"" << b.source << '"';
            for (double v : {b.fps, b.frameMs, b.jitter, b.p99, b.delay, b.gpu}) o << ',' << QString::number(v, 'f', 3);
            for (double v : b.stages) o << ',' << (v < 0 ? QString() : QString::number(v, 'f', 3));
            o << ',' << b.gpuFrames << ',' << QString::number(b.cpu, 'f', 1) << ',' << QString::number(b.rssMb, 'f', 0)
              << ",\"" << b.threads << "\"," << b.flag << ",\"" << b.counters << "\"\n";
        }
        bench_.csv = path;
    }
    bench_.text = complete ? L("Benchmark done (%1 steps). Saved: %2. It is also in the next report.",
                               "Benchmark færdig (%1 trin). Gemt: %2. Den kommer også med i næste rapport.")
                                 .arg(bench_.rows.size()).arg(QFileInfo(path).fileName())
                           : L("Benchmark stopped after %1 of %2 steps; what was measured is saved: %3. %4",
                               "Benchmark stoppet efter %1 af %2 trin; det målte er gemt: %3. %4")
                                 .arg(bench_.rows.size()).arg(bench_.steps.size()).arg(QFileInfo(path).fileName())
                                 .arg(bench_.notes.isEmpty() ? QString() : bench_.notes.last());
    emit benchChanged();
}

void PlayerBridge::stopBenchmark() { stopBenchmark(true); }

void PlayerBridge::stopBenchmark(bool save) {
    if (bench_.timer) { bench_.timer->stop(); bench_.timer->deleteLater(); bench_.timer = nullptr; }
    if (lowLatency_) lowLatency_->setEnabled(QSettings().value("disp/lowLatency", false).toBool());   // as the user set it
    if (save && bench_.running) {   // the stop button: keep what was measured
        bench_.notes << L("Stopped by hand.", "Stoppet i hånden.");
        finishBenchmark();
        return;
    }
    if (bench_.running) {
        bench_.running = false;
        if (bench_.onLowRes && bench_.savedMode.fourcc) {   // the card back to the mode it had
            bench_.onLowRes = false;
            startCapture(bench_.device, bench_.savedMode, bench_.audio);
        }
        s_ = bench_.saved;
        applyEnhance();
    }
    emit benchChanged();
}

QString PlayerBridge::benchTable() const {
    QStringList r;
    r << QStringLiteral("Source: %1%2").arg(bench_.source, bench_.csv.isEmpty() ? "" : " · CSV: `" + bench_.csv + "`");
    for (const QString& n : bench_.notes) r << "- " + n;
    // the cost of each step against the plain picture of the same source
    double base = -1, baseLow = -1;
    for (const auto& b : bench_.rows) {
        if (b.name == "Enhance on, nothing else") base = b.gpu;
        if (b.name == "1080p: enhance on, nothing else") baseLow = b.gpu;
    }
    QString section;
    for (const auto& b : bench_.rows) {
        if (b.section != section) {
            section = b.section;
            r << "" << "**" + section + "**" << ""
              << "| Step | Source | fps | jitter ms | 99 % ms | delay ms | GPU ms | + vs plain | input | denoise/AF | AA | upscale | finish | output | frame gen | CPU % | busiest threads (% of a core) | |"
              << "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|---|";
        }
        const double ref = b.name.startsWith("1080p") ? baseLow : base;
        QString line = QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 | %7 | %8").arg(b.name, b.source).arg(b.fps, 0, 'f', 1).arg(b.jitter, 0, 'f', 2)
                           .arg(b.p99, 0, 'f', 1).arg(b.delay, 0, 'f', 1).arg(b.gpu, 0, 'f', 2)
                           .arg(ref >= 0 && b.gpu >= 0 ? QStringLiteral("%1%2").arg(b.gpu - ref >= 0 ? "+" : "").arg(b.gpu - ref, 0, 'f', 2) : QStringLiteral("–"));
        for (double v : b.stages) line += v < 0 ? QStringLiteral(" | –") : QStringLiteral(" | %1").arg(v, 0, 'f', 2);
        line += QStringLiteral(" | %1 | %2 | %3 |").arg(b.cpu, 0, 'f', 0).arg(b.threads, b.flag == "uneven" ? "⚠ uneven" : b.flag);
        r << line;
    }
    // how much of a frame the heaviest step uses
    double worst = 0, frame = 0;
    QString worstName;
    for (const auto& b : bench_.rows) if (b.gpu > worst && !b.name.startsWith("Enhance off")) { worst = b.gpu; worstName = b.name; frame = b.frameMs; }
    if (worst > 0)
        r << "" << QStringLiteral("Heaviest step: %1, %2 ms GPU (%3 % of a %4 fps frame, %5 % of a 60 fps frame).")
                       .arg(worstName).arg(worst, 0, 'f', 1).arg(frame > 0 ? worst / frame * 100 : 0, 0, 'f', 0)
                       .arg(frame > 0 ? 1000.0 / frame : 0, 0, 'f', 0).arg(worst / 16.667 * 100, 0, 'f', 0);
    return r.join('\n');
}
