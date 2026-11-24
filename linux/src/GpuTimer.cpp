#include "GpuTimer.h"

namespace veyamux {

bool GpuTimer::init(VkPhysicalDevice phys, VkDevice dev, uint32_t queueFamily) {
    destroy();
    VkPhysicalDeviceProperties p{};
    vkGetPhysicalDeviceProperties(phys, &p);
    uint32_t n = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, nullptr);
    if (queueFamily >= n || p.limits.timestampPeriod <= 0) return false;
    VkQueueFamilyProperties qf[32];
    n = n > 32 ? 32 : n;
    vkGetPhysicalDeviceQueueFamilyProperties(phys, &n, qf);
    const uint32_t bits = qf[queueFamily].timestampValidBits;
    if (bits == 0) return false;
    mask_ = bits >= 64 ? ~0ull : ((1ull << bits) - 1);
    periodNs_ = p.limits.timestampPeriod;
    VkQueryPoolCreateInfo ci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    ci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    ci.queryCount = kSlots * kMarks;
    if (vkCreateQueryPool(dev, &ci, nullptr, &pool_) != VK_SUCCESS) { pool_ = VK_NULL_HANDLE; return false; }
    dev_ = dev;
    for (auto& a : age_) a = 1000;
    stage_.fill(-1);
    return true;
}

void GpuTimer::destroy() {
    if (pool_) vkDestroyQueryPool(dev_, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    for (int i = 0; i < kSlots; ++i) { written_[i] = 0; pending_[i] = false; }
}

void GpuTimer::collect(int slot) {
    if (!pending_[slot] || !written_[slot]) return;
    uint64_t r[kMarks * 2] = {};
    const VkResult res = vkGetQueryPoolResults(dev_, pool_, uint32_t(slot * kMarks), kMarks, sizeof r, r, sizeof(uint64_t) * 2,
                                               VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    pending_[slot] = false;
    if (res != VK_SUCCESS && res != VK_NOT_READY) return;
    if (!(written_[slot] & 1u) || !r[1]) return;          // start mark missing / not available
    const uint64_t t0 = r[0] & mask_;
    uint64_t prev = t0, last = t0;
    std::array<float, kStages> st;
    st.fill(-1);
    for (int i = 1; i < kMarks; ++i) {
        if (!(written_[slot] & (1u << i)) || !r[2 * i + 1]) continue;
        const uint64_t t = r[2 * i] & mask_;
        const uint64_t d = (t - prev) & mask_;
        st[size_t(i - 1)] = float(double(d) * periodNs_ * 1e-6);
        prev = t; last = t;
    }
    const float total = float(double((last - t0) & mask_) * periodNs_ * 1e-6);
    if (total < 0 || total > 1000) return;                // wrap-around or garbage
    std::lock_guard g(m_);
    ++sums_.frames;
    sums_.total += total;
    for (int i = 0; i < kStages; ++i)
        if (st[size_t(i)] >= 0) { sums_.stage[size_t(i)] += st[size_t(i)]; ++sums_.ran[size_t(i)]; }
    auto ema = [](float old, float v) { return old < 0 ? v : old + 0.1f * (v - old); };
    total_ = ema(total_, total);
    for (int i = 0; i < kStages; ++i) {
        if (st[size_t(i)] >= 0) { stage_[size_t(i)] = age_[size_t(i)] > 30 ? st[size_t(i)] : ema(stage_[size_t(i)], st[size_t(i)]); age_[size_t(i)] = 0; }
        else if (++age_[size_t(i)] > 30) stage_[size_t(i)] = -1;
    }
}

void GpuTimer::begin(VkCommandBuffer cb, int slot) {
    if (!pool_) return;
    cur_ = ((slot % kSlots) + kSlots) % kSlots;
    collect(cur_);
    vkCmdResetQueryPool(cb, pool_, uint32_t(cur_ * kMarks), kMarks);
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool_, uint32_t(cur_ * kMarks));
    written_[cur_] = 1u;
    pending_[cur_] = true;
}

void GpuTimer::mark(VkCommandBuffer cb, Stage s) {
    if (!pool_ || !pending_[cur_]) return;
    const int i = int(s) + 1;
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, uint32_t(cur_ * kMarks + i));
    written_[cur_] |= 1u << i;
}

void GpuTimer::collectNow(int slot) { if (pool_) collect(((slot % kSlots) + kSlots) % kSlots); }

void GpuTimer::resetSums() { std::lock_guard g(m_); sums_ = Sums{}; }
GpuTimer::Sums GpuTimer::sums() const { std::lock_guard g(m_); return sums_; }

float GpuTimer::totalMs() const { std::lock_guard g(m_); return total_; }
std::array<float, GpuTimer::kStages> GpuTimer::stageMs() const { std::lock_guard g(m_); return stage_; }

} // namespace veyamux
