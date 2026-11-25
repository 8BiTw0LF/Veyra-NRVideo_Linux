#include "Fsr4Upscaler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>

namespace veyamux {

namespace {

// Bindings of the add-on's SPIR-V (VeyAmux-FSR4 build.sh: t -> 0+, u -> 32+, b -> 64+, s -> 80+)
constexpr uint32_t T(uint32_t r) { return r; }
constexpr uint32_t U(uint32_t r) { return 32 + r; }
constexpr uint32_t B(uint32_t r) { return 64 + r; }
constexpr uint32_t S(uint32_t r) { return 80 + r; }

// AMD's provider: the constants (MLSR_Optimized_Constants), 256-byte aligned
struct alignas(16) Constants {
    float inv_size[2], scale[2], inv_scale[2], jitter[2], mv_scale[2], tex_size[2], max_renderSize[2], jitterCancel[2];
    uint32_t width, height, reset, width_lr;
    uint32_t height_lr;
    float preExposure, previous_preExposure;
    uint32_t rcas_enabled;
    float rcas_sharpness, pad;
};
static_assert(sizeof(Constants) <= 256);

uint32_t divUp(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

std::vector<uint32_t> readWords(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto n = size_t(f.tellg());
    if (n == 0 || n % 4) return {};
    std::vector<uint32_t> v(n / 4);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(v.data()), std::streamsize(n));
    return f ? v : std::vector<uint32_t>{};
}

std::vector<char> readBytes(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    std::vector<char> v(size_t(f.tellg()));
    f.seekg(0);
    f.read(v.data(), std::streamsize(v.size()));
    return f ? v : std::vector<char>{};
}

// The binding numbers a SPIR-V module uses (OpDecorate %x Binding n)
std::set<uint32_t> bindingsOf(const std::vector<uint32_t>& spv) {
    std::set<uint32_t> b;
    for (size_t i = 5; i < spv.size();) {
        const uint32_t op = spv[i] & 0xffff, len = spv[i] >> 16;
        if (len == 0) break;
        if (op == 71 /*OpDecorate*/ && len >= 4 && spv[i + 2] == 33 /*Binding*/) b.insert(spv[i + 3]);
        i += len;
    }
    return b;
}

struct LayoutDef { std::vector<std::pair<uint32_t, VkDescriptorType>> b; };
// T(18) is the network's weights (InitializerBuffer): the FP8 pre- and post-pass read them, INT8's do not
const LayoutDef kPre{{{T(0), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE}, {T(1), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE}, {T(2), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
                      {T(3), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE}, {T(4), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE}, {T(6), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
                      {S(0), VK_DESCRIPTOR_TYPE_SAMPLER}, {U(3), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, {U(11), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                      {T(18), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                      {B(0), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}, {B(1), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}}};
const LayoutDef kPass{{{T(1), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, {U(1), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}}};
const LayoutDef kPost{{{T(3), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE}, {T(6), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE}, {T(9), VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
                       {S(0), VK_DESCRIPTOR_TYPE_SAMPLER}, {U(1), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, {U(2), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
                       {U(6), VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}, {U(11), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER}, {T(18), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
                       {B(0), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER}}};

bool fileExists(const std::string& p) { std::ifstream f(p); return bool(f); }
bool fp8Emulated() { const char* e = std::getenv("VEYAMUX_FSR4_FP8"); return e && std::strcmp(e, "emulate") == 0; }
bool fp8Off() { const char* e = std::getenv("VEYAMUX_FSR4_FP8"); return e && std::strcmp(e, "0") == 0; }

bool covers(const LayoutDef& l, const std::set<uint32_t>& used) {
    for (uint32_t u : used) {
        bool found = false;
        for (const auto& [n, t] : l.b) found = found || n == u;
        if (!found) return false;
    }
    return true;
}

} // namespace

Fsr4Upscaler::~Fsr4Upscaler() {
    destroyPipelines();
    destroyResources();
    VkDevice d = ctx_.device();
    if (!d) return;
    if (pool_) vkDestroyDescriptorPool(d, pool_, nullptr);
    for (VkPipelineLayout* p : {&prePl_, &passPl_, &postPl_}) if (*p) vkDestroyPipelineLayout(d, *p, nullptr);
    for (VkDescriptorSetLayout* l : {&preLayout_, &passLayout_, &postLayout_}) if (*l) vkDestroyDescriptorSetLayout(d, *l, nullptr);
    if (sampler_) vkDestroySampler(d, sampler_, nullptr);
}

bool Fsr4Upscaler::supported(const VkContext& ctx, std::string* why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (ctx.apiVersion() < VK_API_VERSION_1_3) return no("needs Vulkan 1.3");
    if (!ctx.caps().intDot) return no("the GPU has no packed 8-bit dot products (shaderIntegerDotProduct)");
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan11Features f11{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES};
    f11.pNext = &f12;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f11;
    vkGetPhysicalDeviceFeatures2(ctx.physical(), &f2);
    if (!f12.shaderInt8 || !f12.shaderFloat16 || !f11.storageBuffer16BitAccess || !f12.storageBuffer8BitAccess)
        return no("the GPU lacks 8/16-bit shader types or storage");
    return true;
}

bool Fsr4Upscaler::fp8Supported(const VkContext& ctx, std::string* why) {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    const auto& c = ctx.caps();
    if (ctx.apiVersion() < VK_API_VERSION_1_3) return no("needs Vulkan 1.3");
    if (!c.coopMatrix) return no("no matrix units in Vulkan (VK_KHR_cooperative_matrix)");
    if (!c.float8) return no("no FP8 matrices (VK_EXT_shader_float8)");
    if (!c.coopConversions) return no("no matrix conversions (VK_EXT_cooperative_matrix_maintenance1)");
    VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    f12.pNext = &f13;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.pNext = &f12;
    vkGetPhysicalDeviceFeatures2(ctx.physical(), &f2);
    if (!f12.vulkanMemoryModel || !f12.shaderFloat16) return no("no Vulkan memory model or 16-bit floats in shaders");
    VkPhysicalDeviceVulkan13Properties p13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES};
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    p2.pNext = &p13;
    vkGetPhysicalDeviceProperties2(ctx.physical(), &p2);
    if (!f13.subgroupSizeControl || p13.minSubgroupSize > 32 || p13.maxSubgroupSize < 32 || !(p13.requiredSubgroupSizeStages & VK_SHADER_STAGE_COMPUTE_BIT))
        return no("cannot run compute in 32-wide waves");
    // the three 16x16x16 shapes the model uses: FP8 x FP8 -> F32, F16 x F16 -> F32, and F16 accumulators
    auto props = reinterpret_cast<PFN_vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR>(
        vkGetInstanceProcAddr(ctx.instance(), "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR"));
    if (!props) return no("no cooperative matrix properties");
    uint32_t n = 0;
    props(ctx.physical(), &n, nullptr);
    std::vector<VkCooperativeMatrixPropertiesKHR> v(n, VkCooperativeMatrixPropertiesKHR{VK_STRUCTURE_TYPE_COOPERATIVE_MATRIX_PROPERTIES_KHR});
    props(ctx.physical(), &n, v.data());
    auto has = [&](VkComponentTypeKHR ab, VkComponentTypeKHR cr) {
        for (const auto& p : v)
            if (p.MSize == 16 && p.NSize == 16 && p.KSize == 16 && p.AType == ab && p.BType == ab && p.CType == cr && p.ResultType == cr &&
                p.scope == VK_SCOPE_SUBGROUP_KHR)
                return true;
        return false;
    };
    if (!has(VK_COMPONENT_TYPE_FLOAT8_E4M3_EXT, VK_COMPONENT_TYPE_FLOAT32_KHR)) return no("no 16x16x16 FP8 matrix multiply");
    if (!has(VK_COMPONENT_TYPE_FLOAT16_KHR, VK_COMPONENT_TYPE_FLOAT32_KHR) || !has(VK_COMPONENT_TYPE_FLOAT16_KHR, VK_COMPONENT_TYPE_FLOAT16_KHR))
        return no("no 16x16x16 F16 matrix multiply");
    return true;
}

void Fsr4Upscaler::setDir(const std::string& dir) {
    if (dir == dir_) return;
    destroyPipelines();
    dir_ = dir;
    fp8Failed_ = 0;
}

bool Fsr4Upscaler::wantFp8() {
    if (!fp8Allowed_) { fp8Why_ = "INT8 chosen"; return false; }
    if (fp8Off()) { fp8Why_ = "VEYAMUX_FSR4_FP8=0"; return false; }
    if (fp8Failed_) return false;   // fp8Why_ says why
    if (!fileExists(dir_ + "/fp8_2160_pre.spv")) { fp8Why_ = "this FSR 4 add-on has no FP8 shaders"; return false; }
    std::string why;
    if (!fp8Emulated() && !fp8Supported(ctx_, &why)) { fp8Why_ = why; return false; }
    fp8Why_.clear();
    return true;
}

VkShaderModule Fsr4Upscaler::module(const std::string& file, std::string& error) {
    const auto spv = readWords(dir_ + "/" + file);
    if (spv.empty()) { error = "FSR 4: missing " + file; return VK_NULL_HANDLE; }
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = spv.size() * 4;
    ci.pCode = spv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(ctx_.device(), &ci, nullptr, &m) != VK_SUCCESS) error = "FSR 4: bad shader " + file;
    return m;
}

void Fsr4Upscaler::destroyPipelines() {
    VkDevice d = ctx_.device();
    if (!d) return;
    vkDeviceWaitIdle(d);   // only when the preset or resolution class changes (rare)
    if (pre_) vkDestroyPipeline(d, pre_, nullptr);
    if (post_) vkDestroyPipeline(d, post_, nullptr);
    for (auto& p : passes_) if (p) vkDestroyPipeline(d, p, nullptr);
    for (auto& p : pads_) if (p) vkDestroyPipeline(d, p, nullptr);
    pre_ = post_ = VK_NULL_HANDLE;
    passes_.fill(VK_NULL_HANDLE);
    pads_.fill(VK_NULL_HANDLE);
    preset_ = res_ = path_ = -1;
    ctx_.destroy(weights_);
    ctx_.destroy(initStaging_);
}

void Fsr4Upscaler::destroyResources() {
    VkDevice d = ctx_.device();
    if (!d) return;
    for (ImageRes* i : {&history_, &reprojected_, &recurrent_, &depth_, &exposure_}) ctx_.destroy(*i);
    if (scratch_) vkDestroyBuffer(d, scratch_, nullptr);
    if (scratchMem_) vkFreeMemory(d, scratchMem_, nullptr);
    if (init_) vkDestroyBuffer(d, init_, nullptr);
    if (initMem_) vkFreeMemory(d, initMem_, nullptr);
    scratch_ = init_ = VK_NULL_HANDLE;
    scratchMem_ = initMem_ = VK_NULL_HANDLE;
    ctx_.destroy(ubo_);
    w_ = h_ = ow_ = oh_ = 0;
    resAlloc_ = -1;
}

bool Fsr4Upscaler::load(int preset, int res, bool fp8, std::string& error) {
    if (preset == preset_ && res == res_ && path_ == (fp8 ? 1 : 0)) return true;
    destroyPipelines();
    VkDevice d = ctx_.device();
    if (!preLayout_) {
        auto layout = [&](const LayoutDef& l, VkDescriptorSetLayout& out) {
            std::vector<VkDescriptorSetLayoutBinding> b;
            for (const auto& [n, t] : l.b) {
                VkDescriptorSetLayoutBinding x{};
                x.binding = n;
                x.descriptorType = t;
                x.descriptorCount = 1;
                x.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
                b.push_back(x);
            }
            VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            ci.bindingCount = uint32_t(b.size());
            ci.pBindings = b.data();
            return vkCreateDescriptorSetLayout(d, &ci, nullptr, &out) == VK_SUCCESS;
        };
        auto pl = [&](VkDescriptorSetLayout l, VkPipelineLayout& out) {
            VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            ci.setLayoutCount = 1;
            ci.pSetLayouts = &l;
            return vkCreatePipelineLayout(d, &ci, nullptr, &out) == VK_SUCCESS;
        };
        if (!layout(kPre, preLayout_) || !layout(kPass, passLayout_) || !layout(kPost, postLayout_) || !pl(preLayout_, prePl_) ||
            !pl(passLayout_, passPl_) || !pl(postLayout_, postPl_)) {
            error = "FSR 4: descriptor layouts";
            return false;
        }
        VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 9 * VkContext::kSlots}, {VK_DESCRIPTOR_TYPE_SAMPLER, 2 * VkContext::kSlots},
                                        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4 * VkContext::kSlots}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6 * VkContext::kSlots},
                                        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 * VkContext::kSlots}};
        VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pci.maxSets = 3 * VkContext::kSlots;
        pci.poolSizeCount = uint32_t(std::size(sizes));
        pci.pPoolSizes = sizes;
        if (vkCreateDescriptorPool(d, &pci, nullptr, &pool_) != VK_SUCCESS) { error = "FSR 4: descriptor pool"; return false; }
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = 0;
        if (vkCreateSampler(d, &si, nullptr, &sampler_) != VK_SUCCESS) { error = "FSR 4: sampler"; return false; }
    }
    const std::string resName = res == 0 ? "1080" : "2160";
    // INT8: shaders per preset; FP8: one set per resolution class (the presets differ only in their weights)
    const std::string base = fp8 ? "fp8_" + resName + "_" : std::string("i8_") + kPresets[preset] + "_" + resName + "_";
    // FP8 needs whole 32-wide waves: one group is one wave, and the matrix code works across its 32 invocations
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo wave32{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_REQUIRED_SUBGROUP_SIZE_CREATE_INFO};
    wave32.requiredSubgroupSize = 32;
    const bool setWave = fp8 && !fp8Emulated();
    auto make = [&](const std::string& file, VkPipelineLayout pl, const LayoutDef& def, VkPipeline& out) {
        const auto spv = readWords(dir_ + "/" + file);
        if (spv.empty()) { error = "FSR 4: missing " + file; return false; }
        if (!covers(def, bindingsOf(spv))) { error = "FSR 4: " + file + " uses bindings VeyAmux does not provide"; return false; }
        VkShaderModule m = module(file, error);
        if (!m) return false;
        VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        ci.stage.pNext = setWave ? &wave32 : nullptr;
        ci.stage.module = m;
        ci.stage.pName = "main";
        ci.layout = pl;
        const VkResult r = vkCreateComputePipelines(d, VK_NULL_HANDLE, 1, &ci, nullptr, &out);
        vkDestroyShaderModule(d, m, nullptr);
        if (r != VK_SUCCESS) { error = "FSR 4: pipeline " + file + " failed (" + std::to_string(int(r)) + ")"; return false; }
        return true;
    };
    if (!make(base + "pre.spv", prePl_, kPre, pre_) || !make(base + "post.spv", postPl_, kPost, post_)) return false;
    for (int i = 1; i <= 12; ++i)
        if (!make(base + "pass" + std::to_string(i) + ".spv", passPl_, kPass, passes_[size_t(i)])) return false;
    if (fp8)
        for (int i = 0; i <= 12; ++i)
            if (i != 6 && !make(base + "pad" + std::to_string(i) + ".spv", passPl_, kPass, pads_[size_t(i)])) return false;
    // the weights: the network's initializers (to a device buffer at the first frame) and, for INT8, the pre-pass's
    const auto init = readBytes(dir_ + "/" + (fp8 ? "fp8_" : "i8_") + kPresets[preset] + "_initializers.bin");
    auto w0 = fp8 ? std::vector<char>(1024, 0) : readBytes(dir_ + "/" + kPresets[preset] + "_pass0_weights.bin");
    if (init.empty() || w0.size() != 1024) { error = "FSR 4: missing the model's weights"; return false; }
    initStaging_ = ctx_.createBuffer(init.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
    weights_ = ctx_.createBuffer(1024, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, false);
    if (!initStaging_.mapped || !weights_.mapped) { error = "FSR 4: out of memory"; return false; }
    std::memcpy(initStaging_.mapped, init.data(), init.size());
    std::memcpy(weights_.mapped, w0.data(), 1024);
    initPending_ = true;
    preset_ = preset;
    res_ = res;
    path_ = fp8 ? 1 : 0;
    resAlloc_ = -1;   // the initializer buffer is (re)made for this model
    return true;
}

bool Fsr4Upscaler::ensureResources(uint32_t w, uint32_t h, uint32_t ow, uint32_t oh, int res, std::string& error) {
    if (w == w_ && h == h_ && ow == ow_ && oh == oh_ && res == resAlloc_ && init_) return true;
    VkDevice d = ctx_.device();
    vkDeviceWaitIdle(d);
    destroyResources();
    const VkImageUsageFlags rw = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    history_ = ctx_.createImage(ow, oh, VK_FORMAT_R16G16B16A16_SFLOAT, rw);
    reprojected_ = ctx_.createImage(ow, oh, VK_FORMAT_R16G16B16A16_SFLOAT, rw);
    recurrent_ = ctx_.createImage(ow, oh, VK_FORMAT_R8G8B8A8_UNORM, rw);
    depth_ = ctx_.createImage(w, h, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    exposure_ = ctx_.createImage(1, 1, VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    const VkDeviceSize scratch = res == 0 ? 20880256 : 83232256;   // AMD's provider (GetScratchSize)
    const bool ok = history_.image && reprojected_.image && recurrent_.image && depth_.image && exposure_.image &&
                    ctx_.createDeviceBuffer(scratch, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, scratch_, scratchMem_) &&
                    ctx_.createDeviceBuffer(initStaging_.size ? initStaging_.size : 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                            init_, initMem_);
    ubo_ = ctx_.createBuffer(256 * VkContext::kSlots, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, false);
    if (!ok || !ubo_.mapped) {
        destroyResources();
        error = "FSR 4: out of GPU memory";
        return false;
    }
    w_ = w; h_ = h; ow_ = ow; oh_ = oh;
    resAlloc_ = res;
    initPending_ = true;
    clearPending_ = true;
    return true;
}

bool Fsr4Upscaler::record(VkCommandBuffer cb, const ImageRes& in, const ImageRes& motion, const ImageRes& out, bool reset, std::string& error) {
    if (dir_.empty()) { error = "FSR 4 is not installed"; return false; }
    const uint32_t w = in.width, h = in.height, ow = out.width, oh = out.height;
    // AMD's network works on sizes that are multiples of 8 (its provider aligns them up)
    if (ow % 8 || oh % 8) { error = "FSR 4 needs an output size that is a multiple of 8"; return false; }
    if (ow > 3840 || oh > 2160) { error = "FSR 4 goes up to 3840x2160"; return false; }
    const int res = (ow > 1920 || oh > 1080) ? 1 : 0;
    const float ratio = float(ow) / float(w);
    int preset = 0;   // AMD's choice by scale factor
    if (ratio >= 1.50f) preset = 1;
    if (ratio >= 1.69f) preset = 2;
    if (ratio >= 1.99f) preset = 3;
    if (ratio >= 2.99f) preset = 4;
    bool fp8 = wantFp8();
    if (fp8 && !load(preset, res, true, error)) {
        // FP8 did not load (a driver that refuses the shaders): INT8 from now on, and say why
        fp8Why_ = "FP8 failed: " + error;
        std::fprintf(stderr, "FSR 4: %s; using INT8\n", fp8Why_.c_str());
        fp8Failed_ = 1;
        destroyPipelines();
        error.clear();
        fp8 = false;
    }
    if (!fp8 && !load(preset, res, false, error)) return false;
    if (!ensureResources(w, h, ow, oh, res, error)) return false;
    VkDevice d = ctx_.device();
    const int sl = ((ctx_.slot() % VkContext::kSlots) + VkContext::kSlots) % VkContext::kSlots;

    auto barrier = [&] {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0, 1, &mb, 0, nullptr, 0, nullptr);
    };
    if (initPending_) {
        VkBufferCopy c{0, 0, initStaging_.size};
        vkCmdCopyBuffer(cb, initStaging_.buffer, init_, 1, &c);
        initPending_ = false;
        reset = true;
    }
    if (clearPending_) {
        const VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkClearColorValue zero{}, one{};
        one.float32[0] = 1.0f;
        vkCmdClearColorImage(cb, depth_.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &r);
        vkCmdClearColorImage(cb, exposure_.image, VK_IMAGE_LAYOUT_GENERAL, &one, 1, &r);
        // the network's tensors start at zero: their borders (padding) are read as zeros, and AMD's border-reset passes
        // (FP8) cover whole groups of 32 only
        vkCmdFillBuffer(cb, scratch_, 0, VK_WHOLE_SIZE, 0);
        clearPending_ = false;
        reset = true;
    }
    barrier();

    // constants
    Constants c{};
    c.inv_size[0] = 1.0f / float(ow); c.inv_size[1] = 1.0f / float(oh);
    c.scale[0] = float(ow) / float(w); c.scale[1] = float(oh) / float(h);
    c.inv_scale[0] = 1.0f / c.scale[0]; c.inv_scale[1] = 1.0f / c.scale[1];
    c.mv_scale[0] = 1.0f / float(w); c.mv_scale[1] = 1.0f / float(h);   // input-pixel vectors to UV
    c.tex_size[0] = float(ow); c.tex_size[1] = float(oh);
    c.max_renderSize[0] = float(w); c.max_renderSize[1] = float(h);
    c.width = ow; c.height = oh; c.reset = reset ? 1 : 0; c.width_lr = w; c.height_lr = h;
    c.preExposure = 1.0f; c.previous_preExposure = prevExposure_;
    prevExposure_ = 1.0f;
    std::memcpy(static_cast<char*>(ubo_.mapped) + size_t(sl) * 256, &c, sizeof c);

    // descriptor sets for this slot (the slot's earlier frame has finished)
    VkDescriptorSet sets[3];
    VkDescriptorSetLayout ls[3] = {preLayout_, passLayout_, postLayout_};
    // one allocation per slot, reused
    static_assert(VkContext::kSlots <= 4);
    if (!setsAllocated_[size_t(sl)]) {
        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = pool_;
        ai.descriptorSetCount = 3;
        ai.pSetLayouts = ls;
        if (vkAllocateDescriptorSets(d, &ai, sets_[size_t(sl)].data()) != VK_SUCCESS) { error = "FSR 4: descriptor sets"; return false; }
        setsAllocated_[size_t(sl)] = true;
    }
    for (int i = 0; i < 3; ++i) sets[i] = sets_[size_t(sl)][size_t(i)];

    std::vector<VkWriteDescriptorSet> wr;
    std::vector<VkDescriptorImageInfo> ii;
    std::vector<VkDescriptorBufferInfo> bi;
    ii.reserve(32);
    bi.reserve(16);
    auto img = [&](VkDescriptorSet s, uint32_t b, VkDescriptorType t, VkImageView v) {
        ii.push_back({VK_NULL_HANDLE, v, VK_IMAGE_LAYOUT_GENERAL});
        VkWriteDescriptorSet x{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        x.dstSet = s; x.dstBinding = b; x.descriptorCount = 1; x.descriptorType = t; x.pImageInfo = &ii.back();
        wr.push_back(x);
    };
    auto smp = [&](VkDescriptorSet s, uint32_t b) {
        ii.push_back({sampler_, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
        VkWriteDescriptorSet x{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        x.dstSet = s; x.dstBinding = b; x.descriptorCount = 1; x.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; x.pImageInfo = &ii.back();
        wr.push_back(x);
    };
    auto buf = [&](VkDescriptorSet s, uint32_t b, VkDescriptorType t, VkBuffer bf, VkDeviceSize off, VkDeviceSize size) {
        bi.push_back({bf, off, size});
        VkWriteDescriptorSet x{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        x.dstSet = s; x.dstBinding = b; x.descriptorCount = 1; x.descriptorType = t; x.pBufferInfo = &bi.back();
        wr.push_back(x);
    };
    const auto SI = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, ST = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, SB = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
               UB = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    // pre: history, motion, depth, colour, recurrent, exposure -> reprojected history + network input (scratch)
    img(sets[0], T(0), SI, history_.view);
    img(sets[0], T(1), SI, motion.view);
    img(sets[0], T(2), SI, depth_.view);
    img(sets[0], T(3), SI, in.view);
    img(sets[0], T(4), SI, recurrent_.view);
    img(sets[0], T(6), SI, exposure_.view);
    smp(sets[0], S(0));
    img(sets[0], U(3), ST, reprojected_.view);
    buf(sets[0], U(11), SB, scratch_, 0, VK_WHOLE_SIZE);
    buf(sets[0], B(0), UB, ubo_.buffer, VkDeviceSize(sl) * 256, 256);
    buf(sets[0], B(1), UB, weights_.buffer, 0, 1024);
    buf(sets[0], T(18), SB, init_, 0, VK_WHOLE_SIZE);
    // network passes: initializers + scratch
    buf(sets[1], T(1), SB, init_, 0, VK_WHOLE_SIZE);
    buf(sets[1], U(1), SB, scratch_, 0, VK_WHOLE_SIZE);
    // post: colour, exposure, reprojected + network output -> picture, history, recurrent
    img(sets[2], T(3), SI, in.view);
    img(sets[2], T(6), SI, exposure_.view);
    img(sets[2], T(9), SI, reprojected_.view);
    smp(sets[2], S(0));
    img(sets[2], U(1), ST, history_.view);
    img(sets[2], U(2), ST, out.view);
    img(sets[2], U(6), ST, recurrent_.view);
    buf(sets[2], U(11), SB, scratch_, 0, VK_WHOLE_SIZE);
    buf(sets[2], T(18), SB, init_, 0, VK_WHOLE_SIZE);
    buf(sets[2], B(0), UB, ubo_.buffer, VkDeviceSize(sl) * 256, 256);
    vkUpdateDescriptorSets(d, uint32_t(wr.size()), wr.data(), 0, nullptr);

    if (path_ == 1) {
        recordFp8(cb, sets, ow, oh);
        return true;
    }
    // dispatches (AMD's provider, the non-WMMA path)
    const uint32_t e0x = ow / 2, e0y = oh / 2, e1x = ow / 4, e1y = oh / 4, e2x = ow / 8, e2y = oh / 8;
    const uint32_t sizes[13][2] = {{0, 0},
                                   {divUp(e0x, 64), e0y}, {divUp(e0x, 64), e0y},
                                   {divUp(e1x, 64), e1y}, {divUp(e1x, 64), e1y}, {divUp(e1x, 64), e1y},
                                   {divUp(e2x, 64), e2y}, {divUp(e2x, 64), e2y}, {divUp(e2x, 64), e2y},
                                   {divUp(e2x, 8), divUp(e2y, 8)},
                                   {divUp(e1x, 64), e1y}, {divUp(e1x, 64), e1y},
                                   {divUp(e0x, 64), e0y}};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pre_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, prePl_, 0, 1, &sets[0], 0, nullptr);
    vkCmdDispatch(cb, divUp(ow, 16), divUp(oh, 16), 1);
    barrier();
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, passPl_, 0, 1, &sets[1], 0, nullptr);
    for (int i = 1; i <= 12; ++i) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, passes_[size_t(i)]);
        vkCmdDispatch(cb, sizes[i][0], sizes[i][1], 1);
        barrier();
    }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, post_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, postPl_, 0, 1, &sets[2], 0, nullptr);
    vkCmdDispatch(cb, divUp(ow, 16), divUp(oh, 16), 1);
    barrier();
    return true;
}

// AMD's provider, the WMMA path (FfxFsr4 Dispatch, supportsWmma): sizes from the output ("upscale") size and the
// network's three encoder levels at 1/2, 1/4 and 1/8 of it; after the pre-pass and each network pass but the 6th, a
// border-reset pass over the output tensor's padding (AMD: GetPaddingAmount / 32, an integer division).
void Fsr4Upscaler::recordFp8(VkCommandBuffer cb, const VkDescriptorSet* sets, uint32_t ow, uint32_t oh) {
    auto barrier = [&] {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    };
    const uint32_t ex[3] = {ow / 2, ow / 4, ow / 8}, ey[3] = {oh / 2, oh / 4, oh / 8};
    struct D { uint32_t x, y; };
    const D sizes[13] = {{0, 0},
                         {divUp(ex[0], 16), divUp(ey[0], 8)}, {divUp(ex[0], 16), divUp(ey[0], 8)},
                         {divUp(ex[1], 32), ey[1]}, {divUp(ex[1], 32), divUp(ey[1], 4)}, {divUp(ex[1], 32), divUp(ey[1], 4)},
                         {divUp(ex[2], 32), ey[2]}, {divUp(ex[2], 32), ey[2]}, {divUp(ex[2], 32), ey[2]}, {divUp(ex[2], 32), ey[2]},
                         {divUp(ex[1], 32), divUp(ey[1], 4)}, {divUp(ex[1], 32), ey[1]},
                         {divUp(ex[0], 16), divUp(ey[0], 8)}};
    const int level[13] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 1, 1, 0, 0};   // the encoder level of each pass's output
    auto padGroups = [&](int i) {
        const int l = level[i];
        return uint32_t((2 * int(ex[l]) + 2 * (int(ey[l]) - 2)) / 32);
    };
    auto pad = [&](int i) {
        if (!pads_[size_t(i)]) return;
        const uint32_t g = padGroups(i);
        if (!g) return;
        barrier();
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pads_[size_t(i)]);
        vkCmdDispatch(cb, g, 1, 1);
    };
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pre_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, prePl_, 0, 1, &sets[0], 0, nullptr);
    vkCmdDispatch(cb, divUp(ow, 64), divUp(oh, 2), 1);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, passPl_, 0, 1, &sets[1], 0, nullptr);
    pad(0);
    barrier();
    for (int i = 1; i <= 12; ++i) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, passes_[size_t(i)]);
        vkCmdDispatch(cb, sizes[i].x, sizes[i].y, 1);
        if (i != 6) pad(i);
        barrier();
    }
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, post_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, postPl_, 0, 1, &sets[2], 0, nullptr);
    vkCmdDispatch(cb, divUp(ow, 64), divUp(oh, 2), 1);
    barrier();
}

} // namespace veyamux
