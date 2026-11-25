// VeyAmux Debug: capture-to-screen timing.
//
// VeyAmux's own delay figure ends when its picture is finished on the GPU. What comes after (waiting for the swapchain
// image, the queue to the display, VSync) is where AMD Anti-Lag / NVIDIA Reflex and VSync make their difference, and
// it is not in that figure. With VEYAMUX_SCREEN_TIMING=1 (a Debug switch; needs a restart), VeyAmux loads its own small
// Vulkan layer, which gives every present an ID (VK_KHR_present_id), and a thread here waits for each new capture
// picture's present to complete (VK_KHR_present_wait): capture timestamp -> on screen.
// With Lossless Scaling, lsfg-vk presents its own pictures too: the present waited for is then the first one after
// VeyAmux's frame, i.e. the first picture on screen made from the new capture picture.
#pragma once
#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vulkan/vulkan.h>

class QQuickWindow;
class LiveVulkan;
namespace veyamux { class VkContext; }

class ScreenTiming : public QObject {
    Q_OBJECT
public:
    ScreenTiming(QQuickWindow* window, veyamux::VkContext* ctx, const QString& layerLib, LiveVulkan* live, QObject* parent = nullptr);
    ~ScreenTiming() override;
    bool available() const { return ok_; }
    QString why() const { return why_; }   // when not available
    struct Sums { double total = 0; int n = 0; int timeouts = 0; };
    Sums sums() const;
    void reset();

private:
    void afterRendering();
    void worker();

    QQuickWindow* win_ = nullptr;
    LiveVulkan* live_ = nullptr;
    VkDevice dev_ = VK_NULL_HANDLE;
    bool ok_ = false;
    QString why_;
    void* layer_ = nullptr;
    VkSwapchainKHR (*llSwapchain_)() = nullptr;
    uint64_t (*llNextPresentId_)() = nullptr;
    PFN_vkWaitForPresentKHR waitForPresent_ = nullptr;

    struct Pending { VkSwapchainKHR sc; uint64_t id; int64_t captured; };
    mutable std::mutex m_;
    std::condition_variable cv_;
    std::deque<Pending> pending_;
    Sums sums_;
    std::atomic<bool> quit_{false};
    std::thread thread_;
};
