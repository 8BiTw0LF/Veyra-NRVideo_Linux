#include "ScreenTiming.h"
#include "LiveVulkan.h"
#include "VkContext.h"

#include <QQuickWindow>
#include <chrono>
#include <dlfcn.h>
#include <pthread.h>

ScreenTiming::ScreenTiming(QQuickWindow* window, veyamux::VkContext* ctx, const QString& layerLib, LiveVulkan* live, QObject* parent)
    : QObject(parent), win_(window), live_(live), dev_(ctx->device()) {
    if (!ctx->caps().presentWait) { why_ = QStringLiteral("off (Debug switch \"Measure to the screen\", or no VK_KHR_present_wait)"); return; }
    if (layerLib.isEmpty()) { why_ = QStringLiteral("VeyAmux's Vulkan layer is not loaded"); return; }
    layer_ = dlopen(layerLib.toLocal8Bit().constData(), RTLD_NOW | RTLD_NOLOAD);
    if (layer_) {
        llSwapchain_ = reinterpret_cast<VkSwapchainKHR (*)()>(dlsym(layer_, "veyamux_ll_swapchain"));
        llNextPresentId_ = reinterpret_cast<uint64_t (*)()>(dlsym(layer_, "veyamux_ll_next_present_id"));
    }
    waitForPresent_ = reinterpret_cast<PFN_vkWaitForPresentKHR>(vkGetDeviceProcAddr(dev_, "vkWaitForPresentKHR"));
    if (!llSwapchain_ || !llNextPresentId_ || !waitForPresent_) { why_ = QStringLiteral("the layer or vkWaitForPresentKHR is missing"); return; }
    ok_ = true;
    qInfo("Screen timing: on (capture -> present complete, VK_KHR_present_wait)");
    connect(window, &QQuickWindow::afterRendering, this, &ScreenTiming::afterRendering, Qt::DirectConnection);
    thread_ = std::thread([this] { pthread_setname_np(pthread_self(), "vx-screentime"); worker(); });
}

ScreenTiming::~ScreenTiming() {
    quit_ = true;
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    if (layer_) dlclose(layer_);
}

// Render thread, after VeyAmux recorded its frame and before Qt presents it: the present ID that present will get.
void ScreenTiming::afterRendering() {
    const int64_t captured = live_->takeDrawnCaptured();
    if (!captured) return;
    const VkSwapchainKHR sc = llSwapchain_();
    if (sc == VK_NULL_HANDLE) return;
    std::lock_guard l(m_);
    if (pending_.size() > 16) pending_.pop_front();   // the waiter fell behind: keep the newest
    pending_.push_back({sc, llNextPresentId_(), captured});
    cv_.notify_one();
}

void ScreenTiming::worker() {
    while (!quit_) {
        Pending p;
        {
            std::unique_lock l(m_);
            cv_.wait_for(l, std::chrono::milliseconds(200), [&] { return quit_ || !pending_.empty(); });
            if (quit_) return;
            if (pending_.empty()) continue;
            p = pending_.front();
            pending_.pop_front();
        }
        // a present ID that is never presented (dropped by a layer below) would wait forever: 250 ms at most
        const VkResult r = waitForPresent_(dev_, p.sc, p.id, 250'000'000ull);
        const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::lock_guard l(m_);
        if (r == VK_SUCCESS) {
            const double ms = double(now - p.captured) / 1e6;
            if (ms > 0 && ms < 500) { sums_.total += ms; ++sums_.n; }
        } else if (r == VK_TIMEOUT) {
            ++sums_.timeouts;
        }
    }
}

ScreenTiming::Sums ScreenTiming::sums() const { std::lock_guard l(m_); return sums_; }
void ScreenTiming::reset() { std::lock_guard l(m_); sums_ = {}; }
