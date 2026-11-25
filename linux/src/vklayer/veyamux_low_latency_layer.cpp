// VeyAmux low-latency layer: the parts of NVIDIA Reflex (VK_NV_low_latency2) that need the swapchain.
//
// Qt Quick creates and presents the window's swapchain itself, and Reflex needs two things there that Qt has no hook
// for: the swapchain must be created with VkSwapchainLatencyCreateInfoNV, and each present must carry a present ID
// (VkPresentIdKHR) that the latency markers refer to. This tiny Vulkan layer, loaded only into VeyAmux (enabled on its
// own instance, never system-wide), adds exactly that and nothing else:
//   - vkCreateSwapchainKHR: chains VkSwapchainLatencyCreateInfoNV{latencyModeEnable} when the device was created with
//     VK_NV_low_latency2, and remembers the swapchain;
//   - vkQueuePresentKHR: chains VkPresentIdKHR with an increasing ID for that swapchain (unless one is there already).
// VeyAmux itself (src/LowLatency.cpp) turns low-latency mode on, sleeps and sets the markers through the driver, and
// reads the swapchain and the next present ID from here (veyamux_ll_swapchain / veyamux_ll_next_present_id).
// VeyAmux Debug's capture-to-screen timing (src/ScreenTiming.cpp, VEYAMUX_SCREEN_TIMING=1) uses the same present IDs
// to wait (VK_KHR_present_wait) until each picture is on the screen; then the layer is loaded on any GPU.
// Everything else passes straight through. Without VK_NV_low_latency2 or VK_KHR_present_id on the device the layer
// changes nothing.
#include <vulkan/vulkan.h>
#include <vulkan/vk_layer.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#define VX_EXPORT extern "C" __attribute__((visibility("default")))

namespace {

using DispatchKey = void*;
DispatchKey key(const void* handle) { return *static_cast<void* const*>(handle); }   // the loader's dispatch table

struct InstanceData {
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    PFN_vkDestroyInstance destroy = nullptr;
};
struct DeviceData {
    VkDevice device = VK_NULL_HANDLE;
    PFN_vkGetDeviceProcAddr gdpa = nullptr;
    PFN_vkDestroyDevice destroy = nullptr;
    PFN_vkCreateSwapchainKHR createSwapchain = nullptr;
    PFN_vkDestroySwapchainKHR destroySwapchain = nullptr;
    PFN_vkQueuePresentKHR present = nullptr;
    bool lowLatency = false, presentId = false;
};

std::mutex gM;
std::unordered_map<DispatchKey, InstanceData> gInstances;
std::unordered_map<DispatchKey, DeviceData> gDevices;   // queues share their device's dispatch key
std::atomic<VkSwapchainKHR> gSwapchain{VK_NULL_HANDLE};
std::atomic<uint64_t> gPresentId{0};

bool hasExt(const VkDeviceCreateInfo* ci, const char* name) {
    for (uint32_t i = 0; i < ci->enabledExtensionCount; ++i)
        if (std::strcmp(ci->ppEnabledExtensionNames[i], name) == 0) return true;
    return false;
}

VKAPI_ATTR VkResult VKAPI_CALL CreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks* alloc, VkInstance* out) {
    auto* link = const_cast<VkLayerInstanceCreateInfo*>(static_cast<const VkLayerInstanceCreateInfo*>(ci->pNext));
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
        link = const_cast<VkLayerInstanceCreateInfo*>(static_cast<const VkLayerInstanceCreateInfo*>(link->pNext));
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;
    const PFN_vkGetInstanceProcAddr next = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    auto create = reinterpret_cast<PFN_vkCreateInstance>(next(VK_NULL_HANDLE, "vkCreateInstance"));
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult r = create(ci, alloc, out);
    if (r != VK_SUCCESS) return r;
    InstanceData d;
    d.gipa = next;
    d.destroy = reinterpret_cast<PFN_vkDestroyInstance>(next(*out, "vkDestroyInstance"));
    std::lock_guard l(gM);
    gInstances[key(*out)] = d;
    return r;
}

VKAPI_ATTR void VKAPI_CALL DestroyInstance(VkInstance inst, const VkAllocationCallbacks* alloc) {
    InstanceData d;
    {
        std::lock_guard l(gM);
        auto it = gInstances.find(key(inst));
        if (it == gInstances.end()) return;
        d = it->second;
        gInstances.erase(it);
    }
    if (d.destroy) d.destroy(inst, alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo* ci, const VkAllocationCallbacks* alloc, VkDevice* out) {
    auto* link = const_cast<VkLayerDeviceCreateInfo*>(static_cast<const VkLayerDeviceCreateInfo*>(ci->pNext));
    while (link && !(link->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && link->function == VK_LAYER_LINK_INFO))
        link = const_cast<VkLayerDeviceCreateInfo*>(static_cast<const VkLayerDeviceCreateInfo*>(link->pNext));
    if (!link) return VK_ERROR_INITIALIZATION_FAILED;
    const PFN_vkGetInstanceProcAddr nextGipa = link->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    const PFN_vkGetDeviceProcAddr nextGdpa = link->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    link->u.pLayerInfo = link->u.pLayerInfo->pNext;
    auto create = reinterpret_cast<PFN_vkCreateDevice>(nextGipa(VK_NULL_HANDLE, "vkCreateDevice"));
    if (!create) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult r = create(phys, ci, alloc, out);
    if (r != VK_SUCCESS) return r;
    DeviceData d;
    d.device = *out;
    d.gdpa = nextGdpa;
    d.destroy = reinterpret_cast<PFN_vkDestroyDevice>(nextGdpa(*out, "vkDestroyDevice"));
    d.createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(nextGdpa(*out, "vkCreateSwapchainKHR"));
    d.destroySwapchain = reinterpret_cast<PFN_vkDestroySwapchainKHR>(nextGdpa(*out, "vkDestroySwapchainKHR"));
    d.present = reinterpret_cast<PFN_vkQueuePresentKHR>(nextGdpa(*out, "vkQueuePresentKHR"));
    d.lowLatency = hasExt(ci, VK_NV_LOW_LATENCY_2_EXTENSION_NAME);
    d.presentId = hasExt(ci, VK_KHR_PRESENT_ID_EXTENSION_NAME);
    std::lock_guard l(gM);
    gDevices[key(*out)] = d;
    return r;
}

bool device(const void* handle, DeviceData& out) {
    std::lock_guard l(gM);
    auto it = gDevices.find(key(handle));
    if (it == gDevices.end()) return false;
    out = it->second;
    return true;
}

VKAPI_ATTR void VKAPI_CALL DestroyDevice(VkDevice dev, const VkAllocationCallbacks* alloc) {
    DeviceData d;
    {
        std::lock_guard l(gM);
        auto it = gDevices.find(key(dev));
        if (it == gDevices.end()) return;
        d = it->second;
        gDevices.erase(it);
    }
    gSwapchain = VK_NULL_HANDLE;
    if (d.destroy) d.destroy(dev, alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL CreateSwapchainKHR(VkDevice dev, const VkSwapchainCreateInfoKHR* ci, const VkAllocationCallbacks* alloc,
                                                  VkSwapchainKHR* out) {
    DeviceData d;
    if (!device(dev, d) || !d.createSwapchain) return VK_ERROR_INITIALIZATION_FAILED;
    if (!d.lowLatency) {   // present IDs only (capture-to-screen timing): remember the swapchain
        const VkResult r = d.createSwapchain(dev, ci, alloc, out);
        if (r == VK_SUCCESS && d.presentId) gSwapchain = *out;
        return r;
    }
    VkSwapchainCreateInfoKHR c = *ci;
    VkSwapchainLatencyCreateInfoNV lat{VK_STRUCTURE_TYPE_SWAPCHAIN_LATENCY_CREATE_INFO_NV};
    lat.latencyModeEnable = VK_TRUE;
    lat.pNext = c.pNext;
    c.pNext = &lat;
    const VkResult r = d.createSwapchain(dev, &c, alloc, out);
    if (r == VK_SUCCESS) gSwapchain = *out;
    return r;
}

VKAPI_ATTR void VKAPI_CALL DestroySwapchainKHR(VkDevice dev, VkSwapchainKHR sc, const VkAllocationCallbacks* alloc) {
    DeviceData d;
    if (!device(dev, d) || !d.destroySwapchain) return;
    VkSwapchainKHR expected = sc;
    gSwapchain.compare_exchange_strong(expected, VK_NULL_HANDLE);
    d.destroySwapchain(dev, sc, alloc);
}

VKAPI_ATTR VkResult VKAPI_CALL QueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pi) {
    DeviceData d;
    if (!device(queue, d) || !d.present) return VK_ERROR_DEVICE_LOST;
    const VkSwapchainKHR sc = gSwapchain.load();
    if (!d.presentId || sc == VK_NULL_HANDLE || pi->swapchainCount != 1 || pi->pSwapchains[0] != sc) return d.present(queue, pi);
    for (auto* s = static_cast<const VkBaseInStructure*>(pi->pNext); s; s = s->pNext)
        if (s->sType == VK_STRUCTURE_TYPE_PRESENT_ID_KHR) return d.present(queue, pi);   // someone set one already
    const uint64_t id = gPresentId.fetch_add(1) + 1;
    VkPresentIdKHR pid{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
    pid.swapchainCount = 1;
    pid.pPresentIds = &id;
    pid.pNext = pi->pNext;
    VkPresentInfoKHR p = *pi;
    p.pNext = &pid;
    return d.present(queue, &p);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice dev, const char* name);

PFN_vkVoidFunction deviceHook(const char* name) {
    if (!std::strcmp(name, "vkGetDeviceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(&GetDeviceProcAddr);
    if (!std::strcmp(name, "vkDestroyDevice")) return reinterpret_cast<PFN_vkVoidFunction>(&DestroyDevice);
    if (!std::strcmp(name, "vkCreateSwapchainKHR")) return reinterpret_cast<PFN_vkVoidFunction>(&CreateSwapchainKHR);
    if (!std::strcmp(name, "vkDestroySwapchainKHR")) return reinterpret_cast<PFN_vkVoidFunction>(&DestroySwapchainKHR);
    if (!std::strcmp(name, "vkQueuePresentKHR")) return reinterpret_cast<PFN_vkVoidFunction>(&QueuePresentKHR);
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetDeviceProcAddr(VkDevice dev, const char* name) {
    if (auto f = deviceHook(name)) return f;
    DeviceData d;
    if (!dev || !device(dev, d) || !d.gdpa) return nullptr;
    return d.gdpa(dev, name);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL GetInstanceProcAddr(VkInstance inst, const char* name) {
    if (!std::strcmp(name, "vkGetInstanceProcAddr")) return reinterpret_cast<PFN_vkVoidFunction>(&GetInstanceProcAddr);
    if (!std::strcmp(name, "vkCreateInstance")) return reinterpret_cast<PFN_vkVoidFunction>(&CreateInstance);
    if (!std::strcmp(name, "vkDestroyInstance")) return reinterpret_cast<PFN_vkVoidFunction>(&DestroyInstance);
    if (!std::strcmp(name, "vkCreateDevice")) return reinterpret_cast<PFN_vkVoidFunction>(&CreateDevice);
    if (auto f = deviceHook(name)) return f;
    if (!inst) return nullptr;
    PFN_vkGetInstanceProcAddr next = nullptr;
    {
        std::lock_guard l(gM);
        auto it = gInstances.find(key(inst));
        if (it != gInstances.end()) next = it->second.gipa;
    }
    return next ? next(inst, name) : nullptr;
}

} // namespace

VX_EXPORT VKAPI_ATTR VkResult VKAPI_CALL vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* v) {
    if (!v || v->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT) return VK_ERROR_INITIALIZATION_FAILED;
    if (v->loaderLayerInterfaceVersion < 2) return VK_ERROR_INITIALIZATION_FAILED;
    v->loaderLayerInterfaceVersion = 2;
    v->pfnGetInstanceProcAddr = &GetInstanceProcAddr;
    v->pfnGetDeviceProcAddr = &GetDeviceProcAddr;
    v->pfnGetPhysicalDeviceProcAddr = nullptr;
    return VK_SUCCESS;
}

// For VeyAmux (looked up with dlopen(RTLD_NOLOAD) + dlsym): the window's swapchain when it was created with latency
// mode, and the present ID the next present will carry.
VX_EXPORT VkSwapchainKHR veyamux_ll_swapchain() { return gSwapchain.load(); }
VX_EXPORT uint64_t veyamux_ll_next_present_id() { return gPresentId.load() + 1; }
