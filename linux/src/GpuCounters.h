// VeyAmux Debug: the graphics card's own hardware counters (VK_KHR_performance_query) around VeyAmux's live work.
//
// Where the timestamps (GpuTimer) say how long a frame took, the counters say why: how busy the shader cores were,
// how much memory was read and written, cache hits. RADV exposes them on RDNA 4 (GFX12) and other recent AMD GPUs.
// Only counters that can be measured around any span of commands in one pass are used (Qt submits the command
// buffer, so VeyAmux cannot add the multi-pass submit info), the most telling ones first by name.
#pragma once
#include <vulkan/vulkan.h>

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace veyamux {

class GpuCounters {
public:
    static constexpr int kSlots = 3;
    bool init(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t queueFamily);
    void destroy();
    bool available() const { return !chosen_.empty(); }
    // Takes the profiling lock (needed while counters are recorded) and starts counting; off stops recording them.
    bool setEnabled(bool on);
    bool enabled() const { return enabled_.load(); }
    void begin(VkCommandBuffer cb, int slot);   // collects the slot's previous result, then starts a query
    void end(VkCommandBuffer cb, int slot);

    struct Value { std::string name, unit, category; double avg = 0; int frames = 0; };
    std::vector<Value> values() const;          // averages since reset()
    void reset();

private:
    void collect(int slot);
    VkDevice dev_ = VK_NULL_HANDLE;
    std::array<VkQueryPool, kSlots> pools_{};
    std::array<bool, kSlots> pending_{};
    std::vector<uint32_t> chosen_;              // counter indices
    std::vector<VkPerformanceCounterKHR> counters_;
    std::vector<VkPerformanceCounterDescriptionKHR> descs_;
    std::atomic<bool> enabled_{false};   // set from the UI thread, read on the render thread
    bool locked_ = false, inQuery_ = false;
    mutable std::mutex m_;
    std::vector<double> sums_;
    int frames_ = 0;
    PFN_vkAcquireProfilingLockKHR acquire_ = nullptr;
    PFN_vkReleaseProfilingLockKHR release_ = nullptr;
};

} // namespace veyamux
