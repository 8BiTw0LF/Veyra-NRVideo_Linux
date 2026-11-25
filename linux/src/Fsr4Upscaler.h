// VeyAmux for Linux: AMD FSR 4 (ML upscaling) through Vulkan.
//
// The model, its weights and the shaders are AMD's (FidelityFX SDK 2.0.0, FSR 4.0.2, MIT), compiled to SPIR-V in the
// separate VeyAmux-FSR4 add-on, which VeyAmux downloads when FSR 4 is chosen (PlayerBridge). This class is the part
// of AMD's provider that VeyAmux needs, rewritten for Vulkan: the resources, the constants and the dispatch order
// (auto exposure off, linear colour, motion vectors at the input resolution, no jitter, no depth).
//
// Per frame: the pre-pass reprojects the history and turns colour, history and the recurrent features into the
// network's input; twelve network passes work in one scratch buffer; the post-pass writes the picture, the history
// and the recurrent features for the next frame.
//
// Two builds of the network, both AMD's:
// * INT8 (fsr4_model_v07_i8): packed 8-bit dot products on the ordinary shader units. Any GPU with Vulkan 1.3's
//   shaderIntegerDotProduct.
// * FP8 (fsr4_model_v07_fp8_no_scale, AMD's own path on RDNA 4): 16x16 FP8 matrix multiplies on the matrix units
//   (VK_KHR_cooperative_matrix + VK_EXT_shader_float8 + VK_EXT_cooperative_matrix_maintenance1's conversions), in
//   one-wave (32-invocation) groups; each network pass is followed by a small pass that resets its output's border.
//   Used when the GPU has all of it and the add-on has the FP8 shaders; INT8 otherwise, or when FP8 fails to load.
//   VEYAMUX_FSR4_FP8=0 never uses it; =emulate loads FP8 shaders without the checks (the add-on's emulated test
//   build, on any GPU).
#pragma once
#include "VkContext.h"

#include <array>
#include <string>
#include <vector>

namespace veyamux {

class Fsr4Upscaler {
public:
    explicit Fsr4Upscaler(VkContext& ctx) : ctx_(ctx) {}
    ~Fsr4Upscaler();
    Fsr4Upscaler(const Fsr4Upscaler&) = delete;
    Fsr4Upscaler& operator=(const Fsr4Upscaler&) = delete;

    // Can this GPU run the INT8 model? (Vulkan 1.3, packed int8 dot products, 16-bit storage, int8 in shaders)
    static bool supported(const VkContext& ctx, std::string* why = nullptr);
    // ... and the FP8 one on the matrix units?
    static bool fp8Supported(const VkContext& ctx, std::string* why = nullptr);
    // false: INT8 only (a benchmark step comparing the two). Takes effect at the next frame.
    void setFp8Allowed(bool on) { fp8Allowed_ = on; }
    // What runs: "FP8" / "INT8" (empty before the first frame), and why FP8 is not used (empty when it is)
    const char* pathName() const { return path_ < 0 ? "" : path_ == 1 ? "FP8" : "INT8"; }
    const std::string& fp8Why() const { return fp8Why_; }
    // The add-on's folder (manifest.json, *.spv, *.bin). Changing it drops what was loaded.
    void setDir(const std::string& dir);
    const std::string& dir() const { return dir_; }

    // Upscales `in` (linear RGB, w x h) into `out` (storage RGBA16F, ow x oh) using `motion` (RG16F at w x h, in input
    // pixels, previous = current + motion). reset: no usable history. Records into cb; false + error if it cannot.
    bool record(VkCommandBuffer cb, const ImageRes& in, const ImageRes& motion, const ImageRes& out, bool reset, std::string& error);
    const char* presetName() const { return preset_ < 0 ? "" : kPresets[preset_]; }

    static constexpr const char* kPresets[] = {"native", "quality", "balanced", "performance", "ultraperf", "drs"};

private:
    bool load(int preset, int res, bool fp8, std::string& error);   // pipelines + weights for a preset and resolution class
    bool wantFp8();
    bool ensureResources(uint32_t w, uint32_t h, uint32_t ow, uint32_t oh, int res, std::string& error);
    void recordFp8(VkCommandBuffer cb, const VkDescriptorSet* sets, uint32_t ow, uint32_t oh);
    void destroyPipelines();
    void destroyResources();
    VkShaderModule module(const std::string& file, std::string& error);

    VkContext& ctx_;
    std::string dir_;
    int preset_ = -1, res_ = -1;                 // loaded pipelines
    int path_ = -1;                              // 0 INT8, 1 FP8
    bool fp8Allowed_ = true;
    int fp8Failed_ = 0;                          // FP8 could not load (do not try again for this add-on folder)
    std::string fp8Why_;
    VkDescriptorSetLayout preLayout_ = VK_NULL_HANDLE, passLayout_ = VK_NULL_HANDLE, postLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout prePl_ = VK_NULL_HANDLE, passPl_ = VK_NULL_HANDLE, postPl_ = VK_NULL_HANDLE;
    VkPipeline pre_ = VK_NULL_HANDLE, post_ = VK_NULL_HANDLE;
    std::array<VkPipeline, 13> passes_{};          // 1..12
    std::array<VkPipeline, 13> pads_{};            // FP8: the border reset after the pre-pass (0) and passes 1..12
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    // resources (sized for the current input/output)
    uint32_t w_ = 0, h_ = 0, ow_ = 0, oh_ = 0;
    int resAlloc_ = -1;
    ImageRes history_, reprojected_, recurrent_, depth_, exposure_;
    VkBuffer scratch_ = VK_NULL_HANDLE, init_ = VK_NULL_HANDLE;
    VkDeviceMemory scratchMem_ = VK_NULL_HANDLE, initMem_ = VK_NULL_HANDLE;
    BufferRes initStaging_, ubo_, weights_;     // ubo_: kSlots x 256 bytes (constants); weights_: the pre-pass weights
    bool initPending_ = false, clearPending_ = false;
    std::array<std::array<VkDescriptorSet, 3>, VkContext::kSlots> sets_{};   // pre, passes, post per slot
    std::array<bool, VkContext::kSlots> setsAllocated_{};
    float prevExposure_ = 1.0f;
};

} // namespace veyamux
