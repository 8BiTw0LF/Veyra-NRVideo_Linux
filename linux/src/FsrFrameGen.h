// VeyAmux for Linux - AMD FidelityFX Super Resolution 3.1.6 frame generation (FidelityFX SDK 1.1.4 framework + SDK 2.4.0 effect code, MIT) on Vulkan.
//
// Same structure as the Windows build's FsrFgPresenter ("frame interpolation into application-owned GPU textures"):
// no swapchain replacement, no presentation thread. Per real frame we record, into the caller's command buffer:
//   1. FidelityFX optical flow on the new frame (motion + scene-change detection),
//   2. a dense motion field built from that flow (video has no engine motion vectors),
//   3. ffxFrameInterpolationPrepare + ffxFrameInterpolationDispatch -> the frame half-way between the previous
//      frame and this one, written into `output`.
// The previous frame is kept inside the FidelityFX frame interpolation context.
#pragma once
#include "VkContext.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace veyamux {

// Context creation is serialised with ffxBackendLock() (FsrUpscaler.h): the FidelityFX Vulkan backend keeps the device
// in a process-global.

class FsrFrameGen {
public:
    FsrFrameGen(VkContext& ctx, std::string shaderDir);
    ~FsrFrameGen();
    FsrFrameGen(const FsrFrameGen&) = delete;
    FsrFrameGen& operator=(const FsrFrameGen&) = delete;

    // (Re)creates the FidelityFX contexts for this display size. Cheap when the size is unchanged.
    bool ensure(uint32_t width, uint32_t height, std::string& error);

    // `current`: the newest real frame, RGBA8 UNORM (sRGB-coded), GENERAL layout, SAMPLED|STORAGE usage.
    // `output`: same size/format, receives the generated in-between frame. reset=true after seeks/cuts/size changes;
    // a reset frame produces no usable in-between frame (the caller shows the real frame only).
    bool record(VkCommandBuffer cb, const ImageRes& current, const ImageRes& output, bool reset, float frameTimeMs,
                std::string& error);

    // Motion source for FSR's "game motion vector" input: +1 / -1 = optical flow (sign), 0 = none.
    void setMotionSign(float s) { motionSign_ = s; }
    // Keep static on-screen elements (HUD, text, logos) sharp in generated frames: pixels unchanged for
    // kStaticFrames real frames get zero motion and the real pixel.
    void setProtectStatic(bool on) { protectStatic_ = on; }
    // Where the previous and current frame disagree after moving half-way (fine/aliased detail), use sharp pixels from
    // one of them instead of FSR's smeared average (which flickers against the sharp real frames).
    void setKeepDetail(bool on) { keepDetail_ = on; }
    static constexpr float kStaticFrames = 6.0f;
    void release();
    // Test hook: copies the latest optical-flow vectors (int16x2 per 8x8 block) into a host buffer.
    bool debugCopyFlow(VkCommandBuffer cb, VkBuffer dst, uint32_t& w, uint32_t& h);
    static const char* versionString();

private:
    struct Impl;
    VkContext& ctx_;
    std::string shaderDir_;
    std::unique_ptr<Impl> p_;
    float motionSign_ = 1.0f;
    bool protectStatic_ = true;
    bool keepDetail_ = true;
};

} // namespace veyamux
