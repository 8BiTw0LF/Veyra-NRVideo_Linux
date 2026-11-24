// VeyAmux for Linux: "Find the best signal" in the capture dialog.
//
// A capture card offers many modes, and which one works best depends on the whole machine, not only the card: the
// USB controller (4K60 uncompressed is close to what USB 3 carries, and some controllers deliver it unevenly), the
// graphics card (AMD and Intel decode MJPEG through VA-API, NVIDIA through NVDEC, some cannot at all) and the CPU
// (which decodes MJPEG when the graphics card cannot). So instead of guessing from the list, the test plays each
// sensible mode for a few seconds with the user's own picture settings and measures:
//   - how many pictures arrive against what the mode promises, how evenly (jitter), pauses and broken pictures;
//   - the delay inside VeyAmux, the CPU use of the whole program and the GPU time per picture;
//   - for MJPEG: where it is decoded (graphics card or CPU) and how long one picture takes, both ways.
// From that it names the mode with the least delay, the smoothest, the sharpest, and one recommendation.
#include "PlayerBridge.h"
#include "CaptureSource.h"

#include <QFile>
#include <QScreen>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <unistd.h>
#include <linux/videodev2.h>

using veyamux::CaptureMode;

namespace {

constexpr double kSettleMs = 2500;    // after a mode starts: the card and the GPU path settle
constexpr double kMeasureMs = 4000;
constexpr double kNoPictureMs = 10000;
constexpr size_t kMaxCandidates = 8;

double nowMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

long long cpuTicks() {   // this process's user + system time, in clock ticks
    QFile f("/proc/self/stat");
    if (!f.open(QIODevice::ReadOnly)) return 0;
    const QString s = QString::fromLatin1(f.readAll());
    const QStringList p = s.mid(s.lastIndexOf(')') + 2).split(' ');
    return p.size() > 13 ? p[11].toLongLong() + p[12].toLongLong() : 0;
}

// what the GPU path takes uncompressed, best first (P010 is left out: it is the HDR mode, a different question)
int rawRank(uint32_t f) {
    switch (f) {
    case V4L2_PIX_FMT_NV12: return 0;
    case V4L2_PIX_FMT_YUYV: return 1;
    case V4L2_PIX_FMT_UYVY: return 2;
    default: return -1;
    }
}
bool isMjpeg(uint32_t f) { return f == V4L2_PIX_FMT_MJPEG || f == V4L2_PIX_FMT_JPEG; }
long pixels(const CaptureMode& m) { return long(m.w) * m.h; }

// The modes worth measuring: the sharpest uncompressed one at its highest rate (and at 30 fps), 1080p uncompressed,
// a size in between when it reaches a higher rate than the largest, and MJPEG at its largest size (graphics card and
// CPU), plus MJPEG 1080p when that is faster than 1080p uncompressed.
std::vector<CaptureMode> pickModes(const std::vector<CaptureMode>& all, std::vector<int>& decodes) {
    std::vector<CaptureMode> raw, mj;
    for (const auto& m : all) {
        // below 25 is no use for games; above 120 no console sends (a 240 fps mode only repeats pictures over USB)
        if (m.fps < 24.5 || m.fps > 121) continue;
        if (rawRank(m.fourcc) >= 0) raw.push_back(m);
        else if (isMjpeg(m.fourcc)) mj.push_back(m);
    }
    // the best mode of a size: the highest rate (or the one nearest to `fps`), then the best format
    auto best = [](const std::vector<CaptureMode>& v, auto sizeOk, double fps = 0) -> const CaptureMode* {
        const CaptureMode* b = nullptr;
        for (const auto& m : v) {
            if (!sizeOk(m)) continue;
            if (fps > 0 && std::abs(m.fps - fps) > 1.5) continue;
            if (!b) { b = &m; continue; }
            if (pixels(m) != pixels(*b)) { if (pixels(m) > pixels(*b)) b = &m; continue; }
            if (fps <= 0 && std::abs(m.fps - b->fps) > 0.5) { if (m.fps > b->fps) b = &m; continue; }
            if (rawRank(m.fourcc) >= 0 && rawRank(b->fourcc) >= 0 && rawRank(m.fourcc) < rawRank(b->fourcc)) b = &m;
            else if (std::abs(m.fps - 60.0) < std::abs(b->fps - 60.0)) b = &m;   // 60 over 59.94 for the same size
        }
        return b;
    };
    auto any = [](const CaptureMode&) { return true; };
    std::vector<CaptureMode> out;
    auto add = [&](const CaptureMode* m, int decode) {
        if (!m || out.size() >= kMaxCandidates) return;
        for (size_t i = 0; i < out.size(); ++i)
            if (out[i].fourcc == m->fourcc && out[i].w == m->w && out[i].h == m->h && std::abs(out[i].fps - m->fps) < 0.1 && decodes[i] == decode) return;
        out.push_back(*m);
        decodes.push_back(decode);
    };
    const CaptureMode* top = best(raw, any);
    if (top) {
        add(top, 0);
        const long tp = pixels(*top);
        const int tw = top->w, th = top->h;
        auto same = [tw, th](const CaptureMode& m) { return m.w == tw && m.h == th; };
        if (top->fps > 31) add(best(raw, same, 30.0), 0);
        // 1080p at the same rate as the largest, so the two compare like with like (else its highest rate)
        auto is1080 = [](const CaptureMode& m) { return m.w == 1920 && m.h == 1080; };
        const CaptureMode* hd = best(raw, is1080, top->fps);
        add(hd ? hd : best(raw, is1080), 0);
        // in between, 16:9 where possible, only when it is faster than the largest
        const double topFps = top->fps;
        const CaptureMode* mid = best(raw, [tp, topFps](const CaptureMode& m) {
            return pixels(m) < tp && pixels(m) > 1920L * 1080 && m.fps > topFps + 1 && std::abs(m.w * 9 - m.h * 16) < m.w / 10;
        });
        if (!mid) mid = best(raw, [tp, topFps](const CaptureMode& m) { return pixels(m) < tp && pixels(m) > 1920L * 1080 && m.fps > topFps + 1; });
        add(mid, 0);
    }
    if (const CaptureMode* m = best(mj, any)) { add(m, 1); add(m, 2); }
    // a card without uncompressed 1080p: its MJPEG 1080p
    if (!best(raw, [](const CaptureMode& m) { return m.w == 1920 && m.h == 1080; }))
        add(best(mj, [](const CaptureMode& m) { return m.w == 1920 && m.h == 1080; }), 1);
    return out;
}

} // namespace

bool PlayerBridge::startSignalTest(const QString& path, const QString& audioSource) {
    if (sig_.running || bench_.running) return false;
    if (path.isEmpty() || veyamux::CaptureSource::isScreenCast(path.toStdString()) || veyamux::CaptureSource::isDemo(path.toStdString())
        || veyamux::CaptureSource::isRemotePlay(path.toStdString()))
        return false;
    sig_ = SigTest{};
    std::vector<int> decodes;
    const auto modes = pickModes(veyamux::listCaptureModes(path.toStdString()), decodes);
    if (modes.size() < 2) {
        sig_.text = L("This card has only one usable mode: there is nothing to compare.", "Kortet har kun én brugbar tilstand: der er intet at sammenligne.");
        emit signalTestChanged();
        return false;
    }
    for (size_t i = 0; i < modes.size(); ++i) sig_.cands.push_back({modes[i], isMjpeg(modes[i].fourcc) ? decodes[i] : 0});
    sig_.path = path;
    sig_.audio = audioSource;
    sig_.hadCapture = engine_.isLive() && curMode_.fourcc != 0 && !curDevice_.isEmpty();
    sig_.savedMode = curMode_;
    sig_.savedDevice = curDevice_;
    sig_.savedAudio = curAudio_;
    sig_.running = true;
    sig_.t0 = nowMs();
    sig_.timer = new QTimer(this);
    connect(sig_.timer, &QTimer::timeout, this, [this] { signalTick(); });
    sig_.timer->start(100);
    qInfo("Signal test: %d modes on %s", int(sig_.cands.size()), qPrintable(path));
    signalTick();
    return true;
}

void PlayerBridge::stopSignalTest() {
    if (!sig_.running) return;
    sig_.text = L("Stopped. What was measured so far is shown below.", "Stoppet. Det der nåede at blive målt, står herunder.");
    finishSignalTest(true);
}

void PlayerBridge::signalTick() {
    if (!sig_.running) return;
    const double t = nowMs();
    if (sig_.i >= sig_.cands.size()) { sig_.text.clear(); finishSignalTest(true); return; }
    const SigCand& c = sig_.cands[sig_.i];
    const QString what = QString::fromStdString(c.mode.label)
                       + (c.decode == 1 ? L(" · decoded on the graphics card", " · afkodet på grafikkortet")
                          : c.decode == 2 ? L(" · decoded on the CPU", " · afkodet på CPU'en") : QString());
    auto nextCandidate = [&](SigRow row) {
        sig_.rows.push_back(std::move(row));
        ++sig_.i;
        sig_.phase = 0;
        sig_.tries = 0;
        sig_.t0 = nowMs();
    };

    if (sig_.phase == 0) {   // start the mode
        if (sig_.tries > 0 && t - sig_.t0 < 1500) return;
        sig_.text = L("Testing %1 of %2: %3", "Tester %1 af %2: %3").arg(sig_.i + 1).arg(sig_.cands.size()).arg(what);
        emit signalTestChanged();
        sig_.decodeOverride = c.decode == 2 ? veyamux::FrameConverter::kMjpegCpu
                            : c.decode == 1 ? veyamux::FrameConverter::kMjpegGpu : veyamux::FrameConverter::kMjpegAuto;
        const bool ok = startCapture(sig_.path, c.mode, sig_.audio);
        if (!ok) {
            if (++sig_.tries < 3) { sig_.t0 = nowMs(); return; }
            SigRow r; r.mode = c.mode; r.decode = c.decode; r.nominal = c.mode.fps;
            r.flag = L("Did not start: %1", "Startede ikke: %1").arg(lastCaptureError_);
            nextCandidate(std::move(r));
            return;
        }
        sig_.phase = 1;
        sig_.t0 = nowMs();
        return;
    }
    if (sig_.phase == 1) {   // settle: until pictures flow, then a little longer
        const auto st = engine_.captureStats();
        if (st.delivered == 0 || !liveVulkanActive()) {
            if (t - sig_.t0 > kNoPictureMs) {
                SigRow r; r.mode = c.mode; r.decode = c.decode; r.nominal = c.mode.fps;
                r.flag = L("No pictures came in this mode.", "Der kom ingen billeder i denne tilstand.");
                // nothing at all from the first mode: most likely no signal (the console is off), not the mode
                if (sig_.rows.empty() && sig_.i == 0) {
                    sig_.rows.push_back(r);
                    sig_.text = L("No picture from the card. Turn the console on (and out of rest mode), then try again.",
                                  "Intet billede fra kortet. Tænd konsollen (og væk den fra hvile), og prøv igen.");
                    finishSignalTest(true);
                    return;
                }
                nextCandidate(std::move(r));
            }
            return;
        }
        if (t - sig_.t0 < kSettleMs) return;
        sig_.phase = 2;
        sig_.t0 = t;
        engine_.takeCaptureArrivals();
        sig_.del0 = st.delivered;
        sig_.cor0 = st.corrupt;
        sig_.cpu0 = cpuTicks();
        sig_.delaySum = 0;
        sig_.delayN = 0;
        if (liveVk_) liveVk_->resetGpuSums();
        sig_.text = L("Measuring %1 of %2: %3", "Måler %1 af %2: %3").arg(sig_.i + 1).arg(sig_.cands.size()).arg(what);
        emit signalTestChanged();
        return;
    }
    // phase 2: measure
    if (liveVulkanActive() && liveVk_->pipelineMs() > 0) { sig_.delaySum += liveVk_->pipelineMs(); ++sig_.delayN; }
    if (t - sig_.t0 < kMeasureMs) return;
    const double secs = (t - sig_.t0) / 1000.0;
    const auto st = engine_.captureStats();
    const std::vector<double> iv = engine_.takeCaptureArrivals();
    SigRow r;
    r.mode = c.mode;
    r.decode = c.decode;
    r.measured = true;
    r.nominal = c.mode.fps;
    const uint64_t got = st.delivered - sig_.del0;
    r.fps = double(got) / secs;
    const double interval = 1000.0 / std::max(1.0, r.nominal);
    // pauses: gaps between the card's pictures as they came off USB
    for (double v : iv) if (v > 1.5 * interval) ++r.drops;
    // evenness: as the pictures were drawn, which is what you see. The moment a picture leaves the card is nearly
    // always even; what is not is when it is ready (USB transfer of a large picture, decoding), and that shows here.
    double arrivalSd = 0;
    auto sd = [](const std::vector<double>& v) {
        if (v.size() < 4) return 0.0;
        double mean = 0, var = 0;
        for (double x : v) mean += x;
        mean /= double(v.size());
        for (double x : v) var += (x - mean) * (x - mean);
        return std::sqrt(var / double(v.size()));
    };
    arrivalSd = sd(iv);
    if (liveVk_) {
        const std::vector<double> shown = liveVk_->shownTimes();
        std::vector<double> gaps;
        for (size_t k = 1; k < shown.size(); ++k) if (shown[k - 1] >= sig_.t0) gaps.push_back(shown[k] - shown[k - 1]);
        r.jitter = gaps.size() >= 4 ? sd(gaps) : arrivalSd;
    } else {
        r.jitter = arrivalSd;
    }
    r.corruptPct = got ? 100.0 * double(st.corrupt - sig_.cor0) / double(got) : 0;
    r.delay = sig_.delayN ? sig_.delaySum / sig_.delayN : 0;
    r.cpu = double(cpuTicks() - sig_.cpu0) / double(sysconf(_SC_CLK_TCK)) / secs * 100.0;
    if (liveVk_) { const auto g = liveVk_->gpuSums(); r.gpu = g.frames ? g.total / g.frames : 0; }
    // what the game itself sends: a 120 fps mode with a 60 fps game is no smoother than a 60 fps mode
    if (const auto ct = engine_.liveContent(); ct.contentFps > 1 && !ct.staticPicture) r.content = ct.contentFps;
    r.decodePath = QString::fromStdString(st.decodePath);
    r.decodeMs = st.decodeMs;
    r.copyMs = st.copyMs;
    if (c.decode == 1 && r.decodePath == "CPU") {
        // the graphics card could not decode it (no VA-API / NVDEC JPEG decoder): it ran on the CPU instead, so the
        // separate CPU run of this mode would measure the same thing
        r.decode = 2;
        r.flag = L("The graphics card cannot decode MJPEG here, so the CPU did it.", "Grafikkortet kan ikke afkode MJPEG her, så CPU'en gjorde det.");
        for (size_t k = sig_.i + 1; k < sig_.cands.size(); ++k)
            if (sig_.cands[k].decode == 2 && sig_.cands[k].mode.fourcc == c.mode.fourcc && sig_.cands[k].mode.w == c.mode.w
                && sig_.cands[k].mode.h == c.mode.h && std::abs(sig_.cands[k].mode.fps - c.mode.fps) < 0.1) {
                sig_.cands.erase(sig_.cands.begin() + long(k));
                break;
            }
    }
    const bool fpsOk = r.fps >= 0.97 * r.nominal;
    const bool cleanOk = r.corruptPct < 2.0;
    const bool evenOk = r.drops <= std::max<int>(2, int(0.02 * double(got)));
    r.keepsUp = fpsOk && cleanOk && evenOk;
    if (!fpsOk) r.flag = L("Only %1 of %2 pictures per second arrived (the console may send fewer than this mode asks for, or the USB port cannot carry it).",
                           "Kun %1 af %2 billeder i sekundet kom frem (konsollen sender måske færre, end tilstanden beder om, eller USB-porten kan ikke bære det).")
                             .arg(r.fps, 0, 'f', 1).arg(r.nominal, 0, 'f', 0);
    else if (!cleanOk) r.flag = L("%1 % of the pictures were broken.", "%1 % af billederne var i stykker.").arg(r.corruptPct, 0, 'f', 1);
    else if (!evenOk) r.flag = L("%1 pauses where pictures went missing.", "%1 pauser hvor billeder manglede.").arg(r.drops);
    qInfo("Signal test: %s decode=%d path=%s fps %.2f/%.2f jitter %.2f (arrival %.2f) drops %d corrupt %.2f%% delay %.1f cpu %.0f%% gpu %.2f decode %.1f ms (copy %.1f) content %.1f%s",
          c.mode.label.c_str(), c.decode, qPrintable(r.decodePath), r.fps, r.nominal, r.jitter, arrivalSd, r.drops, r.corruptPct, r.delay, r.cpu, r.gpu,
          r.decodeMs, r.copyMs, r.content, r.keepsUp ? "" : " (does not keep up)");
    nextCandidate(std::move(r));
}

void PlayerBridge::finishSignalTest(bool restore) {
    if (sig_.timer) { sig_.timer->stop(); sig_.timer->deleteLater(); sig_.timer = nullptr; }
    sig_.running = false;
    sig_.decodeOverride = -1;
    // verdicts among the modes that kept up
    std::vector<SigRow*> ok;
    for (auto& r : sig_.rows) { r.tags.clear(); if (r.measured && r.keepsUp) ok.push_back(&r); }
    SigRow* rec = nullptr;
    if (!ok.empty()) {
        double maxFps = 0, bestDelay = 1e9;
        long maxPix = 1;
        // what the console sends, from the highest of the measurements: one game's measured rate wanders with the
        // scene, so each mode is judged against the same value (a 120 fps mode is no smoother when the game sends 60)
        double content = 0;
        for (const auto& r : sig_.rows) if (r.measured) content = std::max(content, r.content);
        auto rate = [content](const SigRow& r) { return content > 1 ? std::min(r.nominal, std::round(content)) : r.nominal; };
        for (auto* r : ok) {
            maxFps = std::max(maxFps, rate(*r));
            maxPix = std::max(maxPix, pixels(r->mode));
            if (r->delay > 0) bestDelay = std::min(bestDelay, r->delay);
        }
        // smoothness first (a game's frame rate is what you feel most), then sharpness (compressed counts a little
        // less), then delay and how evenly the pictures came
        for (auto* r : ok) {
            const double interval = 1000.0 / std::max(1.0, r->nominal);
            r->score = 50.0 * rate(*r) / maxFps
                     + 25.0 * std::sqrt(double(pixels(r->mode)) / double(maxPix)) * (isMjpeg(r->mode.fourcc) ? 0.85 : 1.0)
                     // delay by the milliseconds it adds (a ratio would make 6 against 3 ms look as bad as 40 against 20)
                     + 15.0 * (r->delay > 0 && bestDelay < 1e9 ? 1.0 - std::min(1.0, (r->delay - bestDelay) / 33.0) : 0.5)
                     + 10.0 * (1.0 - std::min(1.0, r->jitter / (0.5 * interval)));
            if (r->decode == 2 && r->cpu > 100.0 * 0.5 * double(sysconf(_SC_NPROCESSORS_ONLN))) r->score -= 10;   // CPU nearly full
        }
        auto pick = [&](auto better) { SigRow* b = nullptr; for (auto* r : ok) if (!b || better(*r, *b)) b = r; return b; };
        rec = pick([](const SigRow& a, const SigRow& b) { return a.score > b.score; });
        SigRow* fast = pick([](const SigRow& a, const SigRow& b) { return a.delay > 0 && (b.delay <= 0 || a.delay < b.delay - 0.05); });
        SigRow* smooth = pick([&](const SigRow& a, const SigRow& b) {
            return rate(a) > rate(b) + 1 || (std::abs(rate(a) - rate(b)) <= 1 && a.jitter < b.jitter);
        });
        SigRow* sharp = pick([](const SigRow& a, const SigRow& b) {
            if (pixels(a.mode) != pixels(b.mode)) return pixels(a.mode) > pixels(b.mode);
            if (isMjpeg(a.mode.fourcc) != isMjpeg(b.mode.fourcc)) return !isMjpeg(a.mode.fourcc);
            return a.nominal > b.nominal;
        });
        if (rec) rec->tags << L("Recommended", "Anbefalet");
        if (fast) fast->tags << L("Least delay", "Mindst forsinkelse");
        if (smooth) smooth->tags << L("Smoothest", "Mest flydende");
        if (sharp) sharp->tags << L("Sharpest", "Skarpest");
    }
    if (sig_.text.isEmpty()) {
        if (rec) sig_.text = L("Done. Recommended on this PC: %1", "Færdig. Anbefalet på denne pc: %1").arg(QString::fromStdString(rec->mode.label));
        else if (!sig_.rows.empty())
            sig_.text = L("Done, but no mode kept up on this PC. Try another USB port (USB 3, directly on the PC).",
                          "Færdig, men ingen tilstand kunne følge med på denne pc. Prøv en anden USB-port (USB 3, direkte på pc'en).");
    }
    if (rec) qInfo("Signal test: recommended %s (decode %d)", rec->mode.label.c_str(), rec->decode);
    if (restore) {
        // back to what was playing; with nothing before, the recommendation stays on (the picture behind the dialog)
        if (sig_.hadCapture) startCapture(sig_.savedDevice, sig_.savedMode, sig_.savedAudio);
        else if (rec) {
            sig_.decodeOverride = rec->decode == 2 ? veyamux::FrameConverter::kMjpegCpu : veyamux::FrameConverter::kMjpegAuto;
            startCapture(sig_.path, rec->mode, sig_.audio);
            sig_.decodeOverride = -1;
        }
    }
    emit signalTestChanged();
}

QVariantList PlayerBridge::signalTestResults() const {
    QVariantList out;
    for (const auto& r : sig_.rows) {
        QVariantMap e;
        QString label = QString::fromStdString(r.mode.label);
        if (r.decode == 1) label += L(" · graphics card", " · grafikkort");
        if (r.decode == 2) label += L(" · CPU", " · CPU");
        e["label"] = label;
        QStringList parts;
        if (r.measured) {
            parts << L("%1 of %2 fps", "%1 af %2 fps").arg(r.fps, 0, 'f', 1).arg(r.nominal, 0, 'f', 0);
            parts << L("even ±%1 ms", "jævnhed ±%1 ms").arg(r.jitter, 0, 'f', 1);
            if (r.delay > 0) parts << L("delay %1 ms", "forsinkelse %1 ms").arg(r.delay, 0, 'f', 0);
            parts << L("CPU %1 %", "CPU %1 %").arg(r.cpu, 0, 'f', 0);
            if (r.gpu > 0) parts << L("GPU %1 ms", "GPU %1 ms").arg(r.gpu, 0, 'f', 1);
            if (!r.decodePath.isEmpty() && r.decodeMs > 0)
                parts << L("decoding %1 ms on %2", "afkodning %1 ms på %2").arg(r.decodeMs, 0, 'f', 1).arg(r.decodePath)
                             + (r.copyMs > 0.05 ? L(" (fetching %1 ms)", " (hentning %1 ms)").arg(r.copyMs, 0, 'f', 1) : QString());
            if (r.corruptPct > 0.05) parts << L("%1 % broken", "%1 % i stykker").arg(r.corruptPct, 0, 'f', 1);
        }
        e["stats"] = parts.join(" · ");
        e["flag"] = r.flag;
        e["tags"] = r.tags;
        e["keepsUp"] = r.keepsUp;
        e["measured"] = r.measured;
        e["recommended"] = !r.tags.isEmpty() && r.tags.first() == L("Recommended", "Anbefalet");
        QStringList favs = QSettings().value("cap/favorites").toStringList();
        e["favorite"] = favs.contains(captureModeKey(r.mode));
        e["mjpeg"] = isMjpeg(r.mode.fourcc);
        out << e;
    }
    return out;
}

bool PlayerBridge::useSignalResult(int row) {
    if (row < 0 || row >= int(sig_.rows.size())) return false;
    if (sig_.running) { sig_.text.clear(); finishSignalTest(false); }
    const SigRow r = sig_.rows[size_t(row)];
    // MJPEG: the decoding that was measured is kept (automatic = the graphics card when it can)
    if (isMjpeg(r.mode.fourcc)) QSettings().setValue("cap/mjpegDecode", r.decode == 2 ? veyamux::FrameConverter::kMjpegCpu : veyamux::FrameConverter::kMjpegAuto);
    QStringList favs = QSettings().value("cap/favorites").toStringList();
    const QString k = captureModeKey(r.mode);
    if (favs.contains(k) && favs.first() != k) { favs.removeAll(k); favs.prepend(k); QSettings().setValue("cap/favorites", favs); }
    return startCapture(sig_.path, r.mode, sig_.audio);
}

bool PlayerBridge::toggleFavoriteSignal(int row) {
    if (row < 0 || row >= int(sig_.rows.size())) return false;
    QStringList favs = QSettings().value("cap/favorites").toStringList();
    const QString k = captureModeKey(sig_.rows[size_t(row)].mode);
    if (favs.contains(k)) favs.removeAll(k);
    else if (favs.size() >= maxFavoriteModes()) return false;
    else favs.prepend(k);
    QSettings().setValue("cap/favorites", favs);
    emit signalTestChanged();
    return true;
}

void PlayerBridge::sampleSignalTest() {
    auto mode = [](uint32_t f, int w, int h, double fps) {
        CaptureMode m; m.fourcc = f; m.w = w; m.h = h; m.fps = fps;
        m.label = QStringLiteral("%1x%2 @ %3 fps · %4").arg(w).arg(h).arg(fps).arg(QString::fromStdString(veyamux::fourccName(f))).toStdString();
        return m;
    };
    auto row = [&](CaptureMode m, int decode, double fps, double content, double jitter, int drops, double delay, double cpu, double gpu,
                   const char* path, double decMs) {
        SigRow r; r.mode = m; r.decode = decode; r.measured = true; r.nominal = m.fps; r.fps = fps; r.content = content; r.jitter = jitter;
        r.drops = drops; r.delay = delay; r.cpu = cpu; r.gpu = gpu; r.decodePath = path; r.decodeMs = decMs;
        r.keepsUp = fps >= 0.97 * m.fps && drops <= 2;
        if (!r.keepsUp) r.flag = L("Only %1 of %2 pictures per second arrived (the console may send fewer than this mode asks for, or the USB port cannot carry it).",
                                   "Kun %1 af %2 billeder i sekundet kom frem (konsollen sender måske færre, end tilstanden beder om, eller USB-porten kan ikke bære det).")
                                     .arg(fps, 0, 'f', 1).arg(m.fps, 0, 'f', 0);
        sig_.rows.push_back(r);
    };
    row(mode(V4L2_PIX_FMT_NV12, 3840, 2160, 60), 0, 60.0, 60, 5.6, 0, 19.8, 30, 1.2, "", 0);
    row(mode(V4L2_PIX_FMT_NV12, 3840, 2160, 30), 0, 30.0, 60, 0.1, 0, 17.2, 9, 1.2, "", 0);
    row(mode(V4L2_PIX_FMT_NV12, 1920, 1080, 60), 0, 59.9, 60, 1.5, 0, 5.8, 10, 0.9, "", 0);
    row(mode(V4L2_PIX_FMT_NV12, 2560, 1440, 120), 0, 60.0, 60, 1.2, 0, 8.1, 14, 1.0, "", 0);
    row(mode(V4L2_PIX_FMT_MJPEG, 3840, 2160, 60), 1, 60.0, 60, 1.1, 0, 26.0, 12, 1.2, "GPU (VA-API)", 8.0);
    row(mode(V4L2_PIX_FMT_MJPEG, 3840, 2160, 60), 2, 59.8, 60, 1.9, 1, 29.5, 104, 1.2, "CPU", 10.2);
    sig_.text.clear();
    finishSignalTest(false);
}
