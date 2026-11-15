// VeyAmux for Linux - GPU time per processing stage, measured with Vulkan timestamp queries.
//
// Every frame slot (the live path records into Qt's frames in flight, the file path into its own buffer) has its own
// range of queries. A slot's results are read when the slot is recorded again - by then its previous frame has
// finished on the GPU (Qt waits for that frame's fence), so reading never stalls. Stage times are smoothed (EMA).
#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <cstdint>
#include <mutex>
#include <string>

namespace veyamux {

class GpuTimer {
public:
    enum Stage { Input = 0, Denoise, AntiAlias, Upscale, Finish, Output, kStages };
    static constexpr int kSlots = 4;
    static constexpr int kMarks = kStages + 1;   // mark 0 = start, mark i+1 = end of stage i

    bool init(VkPhysicalDevice phys, VkDevice dev, uint32_t queueFamily);
    void destroy();
    bool ok() const { return pool_ != VK_NULL_HANDLE; }

    // Collects the slot's previous results, then resets its queries and writes the start mark.
    void begin(VkCommandBuffer cb, int slot);
    // End of a stage (stages that did not run this frame are simply not marked).
    void mark(VkCommandBuffer cb, Stage s);
    // For a buffer that was waited for (file path): collect the results right away.
    void collectNow(int slot);

    // Smoothed GPU milliseconds: total and per stage (-1 = stage did not run recently).
    float totalMs() const;
    std::array<float, kStages> stageMs() const;

private:
    void collect(int slot);
    VkDevice dev_ = VK_NULL_HANDLE;
    VkQueryPool pool_ = VK_NULL_HANDLE;
    double periodNs_ = 1.0;
    uint64_t mask_ = ~0ull;
    int cur_ = 0;
    uint32_t written_[kSlots] = {};   // bit i = mark i written in that slot's last recording
    bool pending_[kSlots] = {};
    mutable std::mutex m_;
    float total_ = -1;
    std::array<float, kStages> stage_{};
    std::array<int, kStages> age_{};   // frames since a stage last ran
};

} // namespace veyamux
