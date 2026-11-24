#include "LiveVulkan.h"
#include <pthread.h>
#include <QCoreApplication>
#include <QEvent>
#include <cstring>
#include <QImage>
#include <QMetaObject>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <algorithm>
#include <QDebug>
#include <chrono>

using namespace veyamux;

LiveVulkan::LiveVulkan(QQuickWindow* window, VkContext* ctx, const QString& shaderDir, QObject* parent)
    : QObject(parent), win_(window), ctx_(ctx), chain_(std::make_unique<EnhanceChain>(*ctx, shaderDir.toStdString())) {
    connect(window, &QQuickWindow::beforeRendering, this, &LiveVulkan::onBeforeRendering, Qt::DirectConnection);
}

LiveVulkan::~LiveVulkan() {
    streamQuit_ = true;
    if (streamThread_.joinable()) streamThread_.join();
}

bool LiveVulkan::init(std::string& error) {
    if (!chain_->init(error)) return false;
    streamThread_ = std::thread([this] { pthread_setname_np(pthread_self(), "vx-obs"); streamLoop(); });
    return true;
}

void LiveVulkan::setStream(int maxHeight, std::function<void(QImage, int)> sink) {
    { std::lock_guard l(sinkM_); sink_ = std::move(sink); }
    chain_->setStreamMaxHeight(maxHeight);
}

void LiveVulkan::setRecord(int maxHeight, std::function<void(veyamux::EnhanceChain::RecFrame&&)> sink) {
    { std::lock_guard l(sinkM_); recSink_ = std::move(sink); }
    chain_->setRecordMaxHeight(maxHeight);
}

void LiveVulkan::setCompare(int mode, const QImage& a, const QImage& b) {
    auto bytes = [](const QImage& img) {
        std::vector<uint8_t> v;
        if (img.isNull()) return v;
        const QImage c = img.convertToFormat(QImage::Format_RGBA8888);
        v.resize(size_t(c.width()) * c.height() * 4);
        for (int y = 0; y < c.height(); ++y) std::memcpy(v.data() + size_t(y) * c.width() * 4, c.constScanLine(y), size_t(c.width()) * 4);
        return v;
    };
    if (mode > 0) chain_->setCompareLabels(bytes(a), a.width(), a.height(), bytes(b), b.width(), b.height());
    chain_->setCompare(mode);
}

void LiveVulkan::requestScreenshot(std::function<void(QImage)> cb) {
    { std::lock_guard l(sinkM_); shotCb_ = std::move(cb); }
    chain_->requestScreenshot();
    { std::lock_guard l(m_); redraw_ = true; }   // render the current picture again even if the source is paused
    if (win_) QMetaObject::invokeMethod(win_, "update", Qt::QueuedConnection);
}

// Collects the OBS stream copies as soon as the GPU has written them (independent of when Qt renders next).
void LiveVulkan::streamLoop() {
    std::vector<EnhanceChain::StreamFrame> frames;
    std::vector<EnhanceChain::RecFrame> rec;
    while (!streamQuit_.load()) {
        rec.clear();
        chain_->pollRecord(rec);
        if (!rec.empty()) {
            std::function<void(EnhanceChain::RecFrame&&)> rs;
            { std::lock_guard l(sinkM_); rs = recSink_; }
            if (rs) for (auto& f : rec) rs(std::move(f));
        }
        EnhanceChain::StreamFrame shot;
        if (chain_->pollScreenshot(shot)) {
            std::function<void(QImage)> cb;
            { std::lock_guard l(sinkM_); cb = std::move(shotCb_); shotCb_ = nullptr; }
            if (cb) {
                QImage img(shot.w, shot.h, QImage::Format_RGBA8888);
                for (int y = 0; y < shot.h; ++y) std::memcpy(img.scanLine(y), shot.rgba.data() + size_t(y) * shot.w * 4, size_t(shot.w) * 4);
                cb(std::move(img));
            }
        }
        frames.clear();
        chain_->pollStream(frames);
        if (!frames.empty()) {
            std::function<void(QImage, int)> sink;
            { std::lock_guard l(sinkM_); sink = sink_; }
            for (auto& f : frames) {
                if (!sink) continue;
                QImage img(f.w, f.h, QImage::Format_RGBA8888);
                for (int y = 0; y < f.h; ++y) std::memcpy(img.scanLine(y), f.rgba.data() + size_t(y) * f.w * 4, size_t(f.w) * 4);
                sink(std::move(img), f.delayMs);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

QString LiveVulkan::lastUpError() const {
    std::lock_guard l(m_);
    return QString::fromStdString(upError_);
}

QString LiveVulkan::lastError() const {
    std::lock_guard l(m_);
    return QString::fromStdString(error_);
}

void LiveVulkan::setDisplayFit(QSize px) {
    if (px.width() == fitW_.load() && px.height() == fitH_.load()) return;
    fitW_ = px.width(); fitH_ = px.height();
    { std::lock_guard l(m_); redraw_ = true; }   // draw the current picture again at the new size
    QMetaObject::invokeMethod(win_, "update", Qt::QueuedConnection);
}

void LiveVulkan::setSettings(const EnhanceSettings& s) {
    {
        std::lock_guard l(m_);
        settings_ = s;
        redraw_ = true;           // re-run the chain on the current frame (matters while paused)
    }
    QMetaObject::invokeMethod(win_, "update", Qt::QueuedConnection);
}

void LiveVulkan::setActive(bool on) {
    active_ = on;
    emit outputChanged();
    if (!on) {
        std::lock_guard l(m_);
        hasNew_ = haveCurrent_ = redraw_ = false;
        mailbox_.hold.reset(); mailbox_.ext = nullptr;   // a waiting capture buffer goes back to the card
        // (current_ belongs to the render thread, which gives its buffer back as soon as the picture is uploaded)
    }
    QMetaObject::invokeMethod(win_, "update", Qt::QueuedConnection);
}

void LiveVulkan::dropPending() {
    std::lock_guard l(m_);
    mailbox_.hold.reset(); mailbox_.ext = nullptr; mailbox_.extSize = 0;
    hasNew_ = false;
}

void LiveVulkan::submit(RawFrame& frame) {
    {
        std::lock_guard l(m_);
        // Switched off (a new source is being opened): keep nothing. A capture buffer held here would keep the old
        // device file open (its mapping holds it), and the card then refuses the new mode as busy.
        if (!active_.load()) return;
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double, std::milli>(now - lastArrival_).count();
        lastArrival_ = now;
        if (dt > 4.0 && dt < 100.0) frameMs_ = 0.85 * frameMs_ + 0.15 * dt;
        // the newest frame always wins; an unrendered older one goes back for reuse. A timeline jump (seek) it carried
        // is not lost with it: the newer frame comes after the jump too.
        const bool carry = hasNew_ && mailbox_.reset;
        frame.arrived = now;
        std::swap(mailbox_, frame);
        if (carry) mailbox_.reset = true;
        frame.reset = false;
        hasNew_ = true;
    }
    requestFrame();
}

// A new picture: draw it now. QQuickWindow::update() goes through QWindow::requestUpdate(), which on Wayland waits for
// the compositor's next frame callback - the picture then waits for the compositor's clock, a varying part of a
// refresh (jitter), on top of the GUI thread's queue. An UpdateRequest event, posted ahead of other events, makes the
// render loop draw at once. VEYAMUX_QT_UPDATE=1: the old way, to compare.
veyamux::GpuTimer::Sums LiveVulkan::gpuSums() const { return chain_ ? chain_->gpuSums() : veyamux::GpuTimer::Sums{}; }
void LiveVulkan::resetGpuSums() { if (chain_) chain_->resetGpuSums(); }

void LiveVulkan::requestFrame() {
    if (!win_) return;
    static const bool qtWay = qEnvironmentVariable("VEYAMUX_QT_UPDATE") == "1";
    if (qtWay) { QMetaObject::invokeMethod(win_, "update", Qt::QueuedConnection); return; }
    QCoreApplication::postEvent(win_, new QEvent(QEvent::UpdateRequest), Qt::HighEventPriority);
}

void LiveVulkan::onBeforeRendering() {
    if (!active_.load()) return;
    bool fresh = false, isNew = false;
    EnhanceSettings s;
    {
        std::lock_guard l(m_);
        s = settings_;
        if (hasNew_) {
            isNew = true;
            std::swap(current_, mailbox_);
            mailbox_.hold.reset(); mailbox_.ext = nullptr;   // the picture before is not drawn again: its buffer goes back
            hasNew_ = false;
            haveCurrent_ = true;
            redraw_ = false;
            fresh = true;
        } else if (redraw_ && haveCurrent_) {
            redraw_ = false;
            fresh = true;
        }
    }
    if (!fresh) {
        // FSR frame generation: the generated frame is on screen; the real one follows half a frame later
        if (chain_->fgShowRealPending() && std::chrono::steady_clock::now() >= realDue_ - std::chrono::microseconds(1500)) {
            win_->beginExternalCommands();
            auto* rif = win_->rendererInterface();
            auto* cb = static_cast<VkCommandBuffer*>(rif->getResource(win_, QSGRendererInterface::CommandListResource));
            if (cb && *cb) chain_->recordShowReal(*cb);
            win_->endExternalCommands();
            noteShown();
        }
        return;
    }

    const auto t0 = std::chrono::steady_clock::now();
    // Where a picture's time goes (ms): capture -> pacer due -> handed to the renderer -> drawing starts, and the gap to
    // the previous drawing. Kept for the diagnostic report; VEYAMUX_DEBUG_PACE=1 also logs every picture.
    if (isNew && current_.captured.time_since_epoch().count()) {   // (a redraw is not a new picture)
        static const bool paceLog = qEnvironmentVariableIsSet("VEYAMUX_DEBUG_PACE");
        auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
        const bool paced = current_.due.time_since_epoch().count() != 0;
        PaceSample p;
        p.due = paced ? ms(current_.captured, current_.due) : 0.0;
        p.handed = paced ? ms(current_.due, current_.arrived) : ms(current_.captured, current_.arrived);
        p.draw = ms(current_.arrived, t0);
        p.total = ms(current_.captured, t0);
        p.gap = prevDrawStart_.time_since_epoch().count() ? ms(prevDrawStart_, t0) : 0.0;
        prevDrawStart_ = t0;
        if (paceLog)
            qInfo("pace: due %+6.1f  handed %+6.1f  draw %+6.1f  total %6.1f  gap %6.1f", p.due, p.handed, p.draw, p.total, p.gap);
        std::lock_guard l(paceM_);
        pace_.push_back(p);
        while (pace_.size() > 900) pace_.pop_front();
    }
    if (current_.reset) { chain_->resetHistory(); current_.reset = false; }   // a video file was seeked
    LiveInput in;
    switch (current_.fmt) {
    case RawFrame::YUYV: in.fmt = LiveInput::YUYV; break;
    case RawFrame::UYVY: in.fmt = LiveInput::UYVY; break;
    case RawFrame::NV12: in.fmt = LiveInput::NV12; break;
    case RawFrame::P010: in.fmt = LiveInput::P010; break;
    default: in.fmt = LiveInput::RGBA; break;
    }
    in.data = current_.bytes();
    in.size = current_.size();
    in.reuse = !current_.ext && current_.data.empty();   // a redraw after the capture buffer went back
    in.w = current_.w; in.h = current_.h; in.stride = current_.stride;
    in.fullRange = current_.fullRange;
    in.time = current_.captured.time_since_epoch().count() ? current_.captured : std::chrono::steady_clock::now();

    win_->beginExternalCommands();
    auto* rif = win_->rendererInterface();
    auto* cb = static_cast<VkCommandBuffer*>(rif->getResource(win_, QSGRendererInterface::CommandListResource));
    std::string err;
    double frameMs;
    { std::lock_guard l(m_); frameMs = frameMs_; }
    s.fitW = fitW_.load(); s.fitH = fitH_.load();   // the window's pixel size for the picture (VideoItem)
    bool ok = cb && *cb && chain_->recordLive(*cb, win_->graphicsStateInfo().currentFrameSlot, in, s, err, float(frameMs));
    win_->endExternalCommands();
    // The picture is in the GPU's upload buffer now: a capture buffer goes back to the card at once. Held longer, its
    // mapping would keep the device file open, and the card could not be reopened (signal change) or switched.
    if (current_.ext) { current_.hold.reset(); current_.ext = nullptr; current_.extSize = 0; }
    if (!ok && in.reuse && err.empty()) return;   // a redraw that has to wait for the next picture
    if (!ok) {
        std::lock_guard l(m_);
        error_ = err.empty() ? "Live path unavailable" : err;
        static int warned = 0;
        if (warned++ < 3) qWarning() << "LiveVulkan: record failed:" << QString::fromStdString(error_);
        return;
    }
    {
        std::lock_guard l(m_);
        error_.clear();
        upError_ = chain_->fgError().empty() ? chain_->upError() : "Frame generation: " + chain_->fgError();
    }
    const ImageRes& real = chain_->liveDisplay();
    const auto tEnd = std::chrono::steady_clock::now();
    lastMs_ = std::chrono::duration<double, std::milli>(tEnd - t0).count();
    // Time inside VeyAmux: from the capture driver's timestamp to the moment the frame's GPU work is queued, plus the
    // measured GPU time - i.e. until the picture is ready for the display (scan-out and the card's own delay excluded).
    {
        std::lock_guard l(m_);
        gpuMs_ = chain_->gpuMs();
        gpuStages_ = chain_->gpuStageMs();
    }
    if (current_.captured.time_since_epoch().count()) {
        const double q = std::chrono::duration<double, std::milli>(tEnd - current_.captured).count();
        if (q >= 0 && q < 1000) {
            const double g = std::max(0.0f, chain_->gpuMs());
            const double v = q + g, o = pipeMs_.load();
            pipeMs_ = o <= 0 ? v : o + 0.1 * (v - o);
        }
    }
    const bool recreated = chain_->liveGeneration() != seenGen_;   // images were recreated (size change)
    seenGen_ = chain_->liveGeneration();
    ++seq_;
    const uint64_t realHandle = uint64_t(reinterpret_cast<uintptr_t>(real.image));
    if (qEnvironmentVariableIsSet("VEYAMUX_DEBUG_FG")) {
        static int real = 0;
        ++real;
        if (real % 10 == 0) qInfo("LiveVulkan: %d frames, GPU %.3f ms, CPU %.2f ms, capture interval %.1f ms, output %ux%u%s%s", real,
                                     double(chain_->gpuMs()), lastMs_.load(), frameMs,
                                     chain_->liveOutput().width, chain_->liveOutput().height,
                                     chain_->upError().empty() ? "" : ", upscaling error: ", chain_->upError().c_str());
    }
    publish(realHandle, int(real.width), int(real.height), recreated, isNew);
    if (chain_->fgShowRealPending()) {
        // show the real frame half a capture interval after the generated one
        const int halfUs = int(std::clamp(frameMs * 500.0, 3000.0, 30000.0));
        realDue_ = tEnd + std::chrono::microseconds(halfUs);
        QMetaObject::invokeMethod(win_, [w = win_, ms = std::max(1, halfUs / 1000 - 1)] {
            QTimer::singleShot(ms, Qt::PreciseTimer, w, [w] { w->update(); });
        }, Qt::QueuedConnection);
    }
}

void LiveVulkan::noteShown() {
    ++shown_;
    const double t = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    std::lock_guard l(timesM_);
    shownTimes_.push_back(t);
    while (shownTimes_.size() > 600) shownTimes_.pop_front();
}

std::vector<double> LiveVulkan::shownTimes() const {
    std::lock_guard l(timesM_);
    return {shownTimes_.begin(), shownTimes_.end()};
}

void LiveVulkan::publish(uint64_t handle, int w, int h, bool recreated, bool newPicture) {
    if (newPicture) noteShown();   // a redraw (settings, pause) is not a new picture: the frame times stay true
    imgW_ = w;
    imgH_ = h;
    image_ = handle;
    if (handle != lastHandle_.load() || recreated) {
        lastHandle_ = handle;
        gen_ = gen_.load() + 1;                     // VideoItem re-wraps the image on its next sync
        emit outputChanged();
    }
}

