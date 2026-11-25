#include "LowLatency.h"
#include "VkContext.h"

#include <QQuickWindow>
#include <dlfcn.h>

LowLatency::LowLatency(QQuickWindow* window, veyamux::VkContext* ctx, const QString& layerLib, QObject* parent)
    : QObject(parent), dev_(ctx->device()) {
    const auto& caps = ctx->caps();
    if (caps.antiLag) {
        antiLagUpdate_ = reinterpret_cast<PFN_vkAntiLagUpdateAMD>(vkGetDeviceProcAddr(dev_, "vkAntiLagUpdateAMD"));
        amd_ = antiLagUpdate_ != nullptr;
    }
    if (!amd_ && caps.lowLatency2 && !layerLib.isEmpty()) {
        // the layer is already loaded by the Vulkan loader; RTLD_NOLOAD only hands us its handle
        layer_ = dlopen(layerLib.toLocal8Bit().constData(), RTLD_NOW | RTLD_NOLOAD);
        if (layer_) {
            llSwapchain_ = reinterpret_cast<VkSwapchainKHR (*)()>(dlsym(layer_, "veyamux_ll_swapchain"));
            llNextPresentId_ = reinterpret_cast<uint64_t (*)()>(dlsym(layer_, "veyamux_ll_next_present_id"));
        }
        auto f = [this](const char* n) { return vkGetDeviceProcAddr(dev_, n); };
        setSleepMode_ = reinterpret_cast<PFN_vkSetLatencySleepModeNV>(f("vkSetLatencySleepModeNV"));
        sleep_ = reinterpret_cast<PFN_vkLatencySleepNV>(f("vkLatencySleepNV"));
        marker_ = reinterpret_cast<PFN_vkSetLatencyMarkerNV>(f("vkSetLatencyMarkerNV"));
        createSemaphore_ = reinterpret_cast<PFN_vkCreateSemaphore>(f("vkCreateSemaphore"));
        destroySemaphore_ = reinterpret_cast<PFN_vkDestroySemaphore>(f("vkDestroySemaphore"));
        waitSemaphores_ = reinterpret_cast<PFN_vkWaitSemaphores>(f("vkWaitSemaphores"));
        nv_ = llSwapchain_ && llNextPresentId_ && setSleepMode_ && sleep_ && marker_ && createSemaphore_ && waitSemaphores_;
        if (nv_) {
            VkSemaphoreTypeCreateInfo t{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
            t.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            ci.pNext = &t;
            nv_ = createSemaphore_(dev_, &ci, nullptr, &sem_) == VK_SUCCESS;
        }
    }
    qInfo("Low latency: %s", amd_ ? "AMD Anti-Lag available" : nv_ ? "NVIDIA Reflex available" : "not available on this GPU/driver");
    if (!available()) return;
    // render thread, in frame order
    connect(window, &QQuickWindow::beforeFrameBegin, this, &LowLatency::frameBegin, Qt::DirectConnection);
    connect(window, &QQuickWindow::beforeRendering, this, &LowLatency::renderStart, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterRendering, this, &LowLatency::renderEnd, Qt::DirectConnection);
    connect(window, &QQuickWindow::afterFrameEnd, this, &LowLatency::frameEnd, Qt::DirectConnection);
}

LowLatency::~LowLatency() {
    if (sem_ && destroySemaphore_) destroySemaphore_(dev_, sem_, nullptr);
    if (layer_) dlclose(layer_);
}

void LowLatency::frameBegin() {
    const bool on = enabled_.load();
    ++frame_;
    if (amd_) {
        if (!on && !amdWasOn_) return;
        VkAntiLagPresentationInfoAMD pi{VK_STRUCTURE_TYPE_ANTI_LAG_PRESENTATION_INFO_AMD};
        pi.stage = VK_ANTI_LAG_STAGE_INPUT_AMD;
        pi.frameIndex = frame_;
        VkAntiLagDataAMD d{VK_STRUCTURE_TYPE_ANTI_LAG_DATA_AMD};
        d.mode = on ? VK_ANTI_LAG_MODE_ON_AMD : VK_ANTI_LAG_MODE_OFF_AMD;
        d.maxFPS = 0;
        d.pPresentationInfo = on ? &pi : nullptr;   // may block briefly: that is the point
        antiLagUpdate_(dev_, &d);
        amdWasOn_ = on;
        return;
    }
    // NVIDIA: low-latency mode on the current swapchain (it changes when Qt recreates it), then sleep until the driver
    // says the frame should start
    frameSc_ = llSwapchain_();
    frameId_ = llNextPresentId_();
    if (frameSc_ == VK_NULL_HANDLE) return;
    if (frameSc_ != modeSet_ || on != modeOn_) {
        VkLatencySleepModeInfoNV m{VK_STRUCTURE_TYPE_LATENCY_SLEEP_MODE_INFO_NV};
        m.lowLatencyMode = on;
        m.lowLatencyBoost = on;
        m.minimumIntervalUs = 0;
        setSleepMode_(dev_, frameSc_, &m);
        modeSet_ = frameSc_;
        modeOn_ = on;
    }
    if (!on) return;
    VkLatencySleepInfoNV s{VK_STRUCTURE_TYPE_LATENCY_SLEEP_INFO_NV};
    s.signalSemaphore = sem_;
    s.value = ++semValue_;
    if (sleep_(dev_, frameSc_, &s) == VK_SUCCESS) {
        VkSemaphoreWaitInfo w{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
        w.semaphoreCount = 1;
        w.pSemaphores = &sem_;
        w.pValues = &semValue_;
        waitSemaphores_(dev_, &w, 100'000'000ull);   // at most 100 ms, should the driver never signal
    }
    nvMarker(VK_LATENCY_MARKER_SIMULATION_START_NV);
    nvMarker(VK_LATENCY_MARKER_INPUT_SAMPLE_NV);
}

void LowLatency::nvMarker(VkLatencyMarkerNV m) {
    if (!nv_ || !modeOn_ || frameSc_ == VK_NULL_HANDLE) return;
    VkSetLatencyMarkerInfoNV i{VK_STRUCTURE_TYPE_SET_LATENCY_MARKER_INFO_NV};
    i.presentID = frameId_;
    i.marker = m;
    marker_(dev_, frameSc_, &i);
}

void LowLatency::renderStart() {
    nvMarker(VK_LATENCY_MARKER_SIMULATION_END_NV);
    nvMarker(VK_LATENCY_MARKER_RENDERSUBMIT_START_NV);
}

void LowLatency::renderEnd() {
    if (amd_ && enabled_.load()) {
        VkAntiLagPresentationInfoAMD pi{VK_STRUCTURE_TYPE_ANTI_LAG_PRESENTATION_INFO_AMD};
        pi.stage = VK_ANTI_LAG_STAGE_PRESENT_AMD;
        pi.frameIndex = frame_;
        VkAntiLagDataAMD d{VK_STRUCTURE_TYPE_ANTI_LAG_DATA_AMD};
        d.mode = VK_ANTI_LAG_MODE_ON_AMD;
        d.pPresentationInfo = &pi;
        antiLagUpdate_(dev_, &d);
        return;
    }
    nvMarker(VK_LATENCY_MARKER_RENDERSUBMIT_END_NV);
    nvMarker(VK_LATENCY_MARKER_PRESENT_START_NV);
}

void LowLatency::frameEnd() {
    nvMarker(VK_LATENCY_MARKER_PRESENT_END_NV);
}
