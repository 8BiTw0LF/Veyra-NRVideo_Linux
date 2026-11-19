// VeyAmux for Linux - AMD FidelityFX Super Resolution 3.1.6 upscaling (FidelityFX SDK 1.1.4 framework + SDK 2.4.0 effect code, MIT) on Vulkan.
//
// FSR 3.1 is a temporal upscaler: it accumulates detail over several frames and needs to know how every pixel moved,
// to a fraction of a pixel. Games hand it engine motion vectors and depth; video has neither, so per frame we record,
// into the caller's command buffer:
//   1. a luma pyramid of the input (LxLuma); the previous frame's pyramid is kept,
//   2. coarse-to-fine Lucas-Kanade motion estimation between the two pyramids (LxLkLevel): sub-pixel accurate from
//      the second frame on (FidelityFX optical flow, used for frame generation, is whole-pixel and needs ~6 frames
//      after every reset - measured on VeyAmux test clips it made FSR worse than a Lanczos-3 upscaler),
//   3. ffxFsr3UpscalerContextDispatch with a constant "infinitely far" depth -> the upscaled frame.
// The history (previous output) lives inside the FSR context; reset=true after seeks, cuts and size changes.
//
// Measured on synthetic panning clips (640x360 -> 1280x720, PSNR against the true 1280x720 frames), FSR 3.1 stays
// below a frame-by-frame Lanczos-3 upscaler: FSR was built for games, which jitter the camera every frame and supply
// exact motion; video does neither, and even small motion errors soften the accumulated history. It is offered as an
// option; SGSR (frame by frame) is the default. Test hooks: VEYAMUX_FSR_UP_CONSTMV="x,y[;x,y]" (known motion), VEYAMUX_FSR_UP_RCAS, VEYAMUX_FSR_UP_DEBUGVIEW.
#pragma once
#include <mutex>
#include "VkContext.h"
#include <memory>
#include <string>

namespace veyamux {

// The FidelityFX Vulkan backend keeps the device in a process-global (ffxGetDeviceVK): every FidelityFX context
// creation is serialised with this lock.
std::mutex& ffxBackendLock();

class FsrUpscaler {
public:
    FsrUpscaler(VkContext& ctx, std::string shaderDir);
    ~FsrUpscaler();
    FsrUpscaler(const FsrUpscaler&) = delete;
    FsrUpscaler& operator=(const FsrUpscaler&) = delete;

    // `input`: linear RGB (0..1) RGBA16F, render size, GENERAL layout, SAMPLED|TRANSFER_SRC usage.
    // `output`: linear RGBA16F, display size, GENERAL layout, STORAGE usage.
    bool record(VkCommandBuffer cb, const ImageRes& input, const ImageRes& output, bool reset, float frameTimeMs,
                std::string& error);
    void setMotionSign(float s) { motionSign_ = s; }
    void setSharpness(float s) { rcas_ = s; }   // AMD RCAS strength 0..1 (0 = off)
    void release();
    static const char* versionString();
    // Test hook: copies the motion field of pyramid level `which` (0 = what FSR used; float16x2 per pixel).
    bool debugCopyMotion(VkCommandBuffer cb, VkBuffer dst, int which = 0);

private:
    bool ensure(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, std::string& error);
    struct Impl;
    VkContext& ctx_;
    std::string shaderDir_;
    std::unique_ptr<Impl> p_;
    float motionSign_ = 1.0f;
    float rcas_ = 0.9f;
};

} // namespace veyamux
