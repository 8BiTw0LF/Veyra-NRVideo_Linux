// VeyAmux for Linux: AMD Anti-Lag (VK_AMD_anti_lag) and NVIDIA Reflex (VK_NV_low_latency2) for the window.
//
// Both make the render thread start a frame as late as it can and still be on time, so a new capture picture waits
// less in queues on the way to the screen. The "input" of a frame is the moment VeyAmux takes the newest capture
// picture (frame begin, before Qt acquires the swapchain image); the "present" is just before Qt presents.
// AMD: called straight on VeyAmux's device. NVIDIA: the swapchain and present IDs come from VeyAmux's own small
// Vulkan layer (src/vklayer), since Qt creates the swapchain. Without either, this does nothing.
#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <vulkan/vulkan.h>

class QQuickWindow;
namespace veyamux { class VkContext; }

class LowLatency : public QObject {
    Q_OBJECT
public:
    // layerLib: the low-latency layer's library when it was enabled on the instance (NVIDIA), else empty
    LowLatency(QQuickWindow* window, veyamux::VkContext* ctx, const QString& layerLib, QObject* parent = nullptr);
    ~LowLatency() override;
    void setEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_.load(); }
    bool available() const { return amd_ || nv_; }
    QString method() const { return amd_ ? QStringLiteral("AMD Anti-Lag") : nv_ ? QStringLiteral("NVIDIA Reflex") : QString(); }

private:
    void frameBegin();
    void renderStart();
    void renderEnd();
    void frameEnd();
    void nvMarker(VkLatencyMarkerNV m);

    VkDevice dev_ = VK_NULL_HANDLE;
    std::atomic<bool> enabled_{false};
    bool amd_ = false, nv_ = false;
    bool amdWasOn_ = false;
    uint64_t frame_ = 0;
    // AMD
    PFN_vkAntiLagUpdateAMD antiLagUpdate_ = nullptr;
    // NVIDIA
    void* layer_ = nullptr;
    VkSwapchainKHR (*llSwapchain_)() = nullptr;
    uint64_t (*llNextPresentId_)() = nullptr;
    PFN_vkSetLatencySleepModeNV setSleepMode_ = nullptr;
    PFN_vkLatencySleepNV sleep_ = nullptr;
    PFN_vkSetLatencyMarkerNV marker_ = nullptr;
    PFN_vkCreateSemaphore createSemaphore_ = nullptr;
    PFN_vkDestroySemaphore destroySemaphore_ = nullptr;
    PFN_vkWaitSemaphores waitSemaphores_ = nullptr;
    VkSemaphore sem_ = VK_NULL_HANDLE;
    uint64_t semValue_ = 0;
    VkSwapchainKHR modeSet_ = VK_NULL_HANDLE;   // the swapchain the sleep mode was set on
    bool modeOn_ = false;
    VkSwapchainKHR frameSc_ = VK_NULL_HANDLE;   // this frame's swapchain and present ID (NVIDIA markers)
    uint64_t frameId_ = 0;
};
