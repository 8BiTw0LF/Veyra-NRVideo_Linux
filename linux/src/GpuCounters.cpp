#include "GpuCounters.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace veyamux {

namespace {

std::string lower(std::string s) {
    for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// how telling a counter is for VeyAmux's compute passes (higher first); 0 = not wanted
int rank(const std::string& name) {
    const std::string n = lower(name);
    static const std::pair<const char*, int> keys[] = {
        {"gpu busy", 100}, {"gpu active", 95}, {"shader busy", 90}, {"shader", 60}, {"busy", 80}, {"utiliz", 80},
        {"vram", 75}, {"memory", 70}, {"read", 55}, {"write", 55}, {"l2", 65}, {"cache", 60}, {"hit", 50}, {"miss", 50},
        {"stall", 60}, {"wave", 50}, {"occupancy", 60}, {"alu", 55}, {"valu", 55}, {"cycles", 30}, {"bandwidth", 70},
    };
    int best = 0;
    for (const auto& [k, r] : keys) if (n.find(k) != std::string::npos) best = std::max(best, r);
    return best;
}

const char* unitName(VkPerformanceCounterUnitKHR u) {
    switch (u) {
    case VK_PERFORMANCE_COUNTER_UNIT_PERCENTAGE_KHR: return "%";
    case VK_PERFORMANCE_COUNTER_UNIT_NANOSECONDS_KHR: return "ns";
    case VK_PERFORMANCE_COUNTER_UNIT_BYTES_KHR: return "bytes";
    case VK_PERFORMANCE_COUNTER_UNIT_BYTES_PER_SECOND_KHR: return "bytes/s";
    case VK_PERFORMANCE_COUNTER_UNIT_KELVIN_KHR: return "K";
    case VK_PERFORMANCE_COUNTER_UNIT_WATTS_KHR: return "W";
    case VK_PERFORMANCE_COUNTER_UNIT_VOLTS_KHR: return "V";
    case VK_PERFORMANCE_COUNTER_UNIT_AMPS_KHR: return "A";
    case VK_PERFORMANCE_COUNTER_UNIT_HERTZ_KHR: return "Hz";
    case VK_PERFORMANCE_COUNTER_UNIT_CYCLES_KHR: return "cycles";
    default: return "";
    }
}

double valueOf(const VkPerformanceCounterResultKHR& r, VkPerformanceCounterStorageKHR st) {
    switch (st) {
    case VK_PERFORMANCE_COUNTER_STORAGE_INT32_KHR: return r.int32;
    case VK_PERFORMANCE_COUNTER_STORAGE_INT64_KHR: return double(r.int64);
    case VK_PERFORMANCE_COUNTER_STORAGE_UINT32_KHR: return r.uint32;
    case VK_PERFORMANCE_COUNTER_STORAGE_UINT64_KHR: return double(r.uint64);
    case VK_PERFORMANCE_COUNTER_STORAGE_FLOAT32_KHR: return r.float32;
    case VK_PERFORMANCE_COUNTER_STORAGE_FLOAT64_KHR: return r.float64;
    default: return 0;
    }
}

} // namespace

bool GpuCounters::init(VkInstance inst, VkPhysicalDevice phys, VkDevice dev, uint32_t queueFamily) {
    dev_ = dev;
    auto enumerate = reinterpret_cast<PFN_vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR>(
        vkGetInstanceProcAddr(inst, "vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR"));
    auto passes = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR>(
        vkGetInstanceProcAddr(inst, "vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR"));
    acquire_ = reinterpret_cast<PFN_vkAcquireProfilingLockKHR>(vkGetDeviceProcAddr(dev, "vkAcquireProfilingLockKHR"));
    release_ = reinterpret_cast<PFN_vkReleaseProfilingLockKHR>(vkGetDeviceProcAddr(dev, "vkReleaseProfilingLockKHR"));
    if (!enumerate || !passes || !acquire_ || !release_) return false;
    uint32_t n = 0;
    if (enumerate(phys, queueFamily, &n, nullptr, nullptr) != VK_SUCCESS || n == 0) return false;
    counters_.assign(n, VkPerformanceCounterKHR{VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_KHR});
    descs_.assign(n, VkPerformanceCounterDescriptionKHR{VK_STRUCTURE_TYPE_PERFORMANCE_COUNTER_DESCRIPTION_KHR});
    if (enumerate(phys, queueFamily, &n, counters_.data(), descs_.data()) != VK_SUCCESS) return false;
    // candidates: measurable around any commands, ranked by how telling they are
    std::vector<uint32_t> cand;
    for (uint32_t i = 0; i < n; ++i)
        if (counters_[i].scope == VK_PERFORMANCE_COUNTER_SCOPE_COMMAND_KHR && rank(descs_[i].name) > 0) cand.push_back(i);
    std::stable_sort(cand.begin(), cand.end(), [&](uint32_t a, uint32_t b) { return rank(descs_[a].name) > rank(descs_[b].name); });
    // greedily as many as fit in one pass (at most 12)
    for (uint32_t c : cand) {
        if (chosen_.size() >= 12) break;
        std::vector<uint32_t> t = chosen_;
        t.push_back(c);
        VkQueryPoolPerformanceCreateInfoKHR pci{VK_STRUCTURE_TYPE_QUERY_POOL_PERFORMANCE_CREATE_INFO_KHR};
        pci.queueFamilyIndex = queueFamily;
        pci.counterIndexCount = uint32_t(t.size());
        pci.pCounterIndices = t.data();
        uint32_t np = 0;
        passes(phys, &pci, &np);
        if (np == 1) chosen_ = std::move(t);
    }
    if (chosen_.empty()) return false;
    for (int s = 0; s < kSlots; ++s) {
        VkQueryPoolPerformanceCreateInfoKHR pci{VK_STRUCTURE_TYPE_QUERY_POOL_PERFORMANCE_CREATE_INFO_KHR};
        pci.queueFamilyIndex = queueFamily;
        pci.counterIndexCount = uint32_t(chosen_.size());
        pci.pCounterIndices = chosen_.data();
        VkQueryPoolCreateInfo qci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        qci.pNext = &pci;
        qci.queryType = VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR;
        qci.queryCount = 1;
        if (vkCreateQueryPool(dev, &qci, nullptr, &pools_[size_t(s)]) != VK_SUCCESS) { destroy(); return false; }
    }
    sums_.assign(chosen_.size(), 0.0);
    std::fprintf(stderr, "GPU counters: %d of %u usable in one pass\n", int(chosen_.size()), n);
    return true;
}

void GpuCounters::destroy() {
    for (auto& p : pools_) if (p) { vkDestroyQueryPool(dev_, p, nullptr); p = VK_NULL_HANDLE; }
    if (locked_ && release_) release_(dev_);
    locked_ = false;
    chosen_.clear();
}

bool GpuCounters::setEnabled(bool on) {
    if (on && !available()) return false;
    if (on && !locked_) {
        VkAcquireProfilingLockInfoKHR li{VK_STRUCTURE_TYPE_ACQUIRE_PROFILING_LOCK_INFO_KHR};
        li.timeout = 100'000'000ull;
        if (acquire_(dev_, &li) != VK_SUCCESS) return false;
        locked_ = true;   // kept until the end: command buffers in flight may still use the queries
    }
    enabled_ = on;
    return true;
}

void GpuCounters::collect(int slot) {
    if (!pending_[size_t(slot)]) return;
    std::vector<VkPerformanceCounterResultKHR> r(chosen_.size());
    const VkResult res = vkGetQueryPoolResults(dev_, pools_[size_t(slot)], 0, 1, r.size() * sizeof(r[0]), r.data(), sizeof(r[0]) * r.size(), 0);
    if (res != VK_SUCCESS) return;   // not ready (or lost): this frame is skipped
    pending_[size_t(slot)] = false;
    std::lock_guard l(m_);
    for (size_t i = 0; i < chosen_.size(); ++i) sums_[i] += valueOf(r[i], counters_[chosen_[i]].storage);
    ++frames_;
}

void GpuCounters::begin(VkCommandBuffer cb, int slot) {
    inQuery_ = false;
    if (!enabled_ || slot < 0 || slot >= kSlots) return;
    collect(slot);
    vkCmdResetQueryPool(cb, pools_[size_t(slot)], 0, 1);
    vkCmdBeginQuery(cb, pools_[size_t(slot)], 0, 0);
    inQuery_ = true;
}

void GpuCounters::end(VkCommandBuffer cb, int slot) {
    if (!inQuery_) return;
    vkCmdEndQuery(cb, pools_[size_t(slot)], 0);
    pending_[size_t(slot)] = true;
    inQuery_ = false;
}

std::vector<GpuCounters::Value> GpuCounters::values() const {
    std::lock_guard l(m_);
    std::vector<Value> v;
    for (size_t i = 0; i < chosen_.size(); ++i) {
        const auto& d = descs_[chosen_[i]];
        Value x;
        x.name = d.name;
        x.category = d.category;
        x.unit = unitName(counters_[chosen_[i]].unit);
        x.frames = frames_;
        x.avg = frames_ ? sums_[i] / frames_ : 0;
        v.push_back(std::move(x));
    }
    return v;
}

void GpuCounters::reset() {
    std::lock_guard l(m_);
    std::fill(sums_.begin(), sums_.end(), 0.0);
    frames_ = 0;
}

} // namespace veyamux
