#include "FsrFrameGen.h"
#include <algorithm>
#include "FsrUpscaler.h"
#include "MotionEstimator.h"
#include <cstdlib>

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_frameinterpolation.h>
#include <FidelityFX/host/ffx_opticalflow.h>

#include <cfloat>
#include <cstring>
#include <fstream>
#include <mutex>

namespace veyamux {
namespace {
constexpr size_t kMaxContexts = 3;      // optical flow + frame interpolation + shared resources (the backend sizes its descriptor pools by this)

std::string ffxErr(const char* what, FfxErrorCode e) {
    std::string r = std::string(what) + " failed (FfxErrorCode " + std::to_string(int(e)) + ")";
    if (e == FFX_ERROR_BACKEND_API_ERROR && *ffxVeyamuxLastBackendError()) r += std::string(" at ") + ffxVeyamuxLastBackendError();
    return r;
}

FfxResourceDescription describe(const ImageRes& img, FfxResourceUsage usage) {
    FfxResourceDescription d{};
    d.type = FFX_RESOURCE_TYPE_TEXTURE2D;
    d.format = ffxGetSurfaceFormatVK(img.format);
    d.width = img.width;
    d.height = img.height;
    d.depth = 1;
    d.mipCount = 1;
    d.flags = FFX_RESOURCE_FLAGS_NONE;
    d.usage = usage;
    return d;
}

// All VeyAmux images live in VK_IMAGE_LAYOUT_GENERAL, which the FidelityFX Vulkan backend maps to the UAV state;
// it transitions what it needs and restores the state it was given.
FfxResource wrap(const ImageRes& img, const wchar_t* name, FfxResourceUsage usage = FFX_RESOURCE_USAGE_UAV) {
    return ffxGetResourceVK(reinterpret_cast<void*>(img.image), describe(img, usage), name, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
}
} // namespace


struct FsrFrameGen::Impl {
    VkContext& ctx;
    std::vector<uint8_t> scratch;
    FfxInterface iface{};
    bool ifaceOk = false;
    FfxOpticalflowContext of{};
    FfxFrameInterpolationContext fi{};
    bool ofOk = false, fiOk = false;
    uint32_t sharedId = 0;
    bool sharedOk = false;
    FfxResourceInternal ofVector{}, ofScd{}, dilDepth{}, dilMv{}, prevDepth{};
    bool resOk = false;
    ImageRes depth, motion, ofView;   // motion: zero field for the test hook (motion sign 0)
    ImageRes prevImg, stable;         // previous real frame + per-pixel "unchanged for n frames" (static UI)
    bool havePrevImg = false;
    VkPipeline staticPipe = VK_NULL_HANDLE, detailPipe = VK_NULL_HANDLE;
    std::unique_ptr<MotionEstimator> estimator;
    uint32_t w = 0, h = 0, ofW = 0, ofH = 0;
    uint64_t frameId = 0;

    explicit Impl(VkContext& c) : ctx(c) {}
    ~Impl() { destroyAll(); if (staticPipe) ctx.destroy(staticPipe); if (detailPipe) ctx.destroy(detailPipe); }

    void destroyAll() {
        if (ctx.device()) vkDeviceWaitIdle(ctx.device());
        if (fiOk) ffxFrameInterpolationContextDestroy(&fi);
        if (ofOk) ffxOpticalflowContextDestroy(&of);
        fiOk = ofOk = false;
        if (resOk && sharedOk) {
            for (FfxResourceInternal r : {ofVector, ofScd, dilDepth, dilMv, prevDepth}) iface.fpDestroyResource(&iface, r, sharedId);
        }
        resOk = false;
        if (sharedOk) iface.fpDestroyBackendContext(&iface, sharedId);
        sharedOk = false;
        ctx.destroyView(ofView);
        ctx.destroy(depth);
        ctx.destroy(motion);
        ctx.destroy(prevImg);
        ctx.destroy(stable);
        havePrevImg = false;
        w = h = 0;
    }
};

FsrFrameGen::FsrFrameGen(VkContext& ctx, std::string shaderDir) : ctx_(ctx), shaderDir_(std::move(shaderDir)) {}
FsrFrameGen::~FsrFrameGen() = default;

const char* FsrFrameGen::versionString() { return "AMD FSR 3.1.6 frame generation (FidelityFX SDK 2.4.0 effect code)"; }

void FsrFrameGen::release() { p_.reset(); }

bool FsrFrameGen::ensure(uint32_t width, uint32_t height, std::string& error) {
    if (p_ && p_->w == width && p_->h == height && p_->fiOk) return true;
    // The FidelityFX Vulkan backend reads the device through a process-global (ffxGetDeviceVK); file playback and
    // the live path each have their own VkDevice, so (re)creation is serialised and re-points that global first.
    std::lock_guard<std::mutex> guard(ffxBackendLock());
    if (!p_) p_ = std::make_unique<Impl>(ctx_);
    Impl& s = *p_;
    s.destroyAll();

    if (!s.ifaceOk) {
        s.scratch.assign(ffxGetScratchMemorySizeVK(ctx_.physical(), kMaxContexts), 0);
        VkDeviceContext dc{ctx_.device(), ctx_.physical(), vkGetDeviceProcAddr};
        const FfxErrorCode e = ffxGetInterfaceVK(&s.iface, ffxGetDeviceVK(&dc), s.scratch.data(), s.scratch.size(), kMaxContexts);
        if (e != FFX_OK) { error = ffxErr("ffxGetInterfaceVK", e); return false; }
        s.ifaceOk = true;
    }
    auto load = [&](const char* name, VkPipeline& out) {
        if (out) return true;
        std::ifstream f(shaderDir_ + "/" + name + ".spv", std::ios::binary | std::ios::ate);
        if (!f) { error = std::string("Missing shader ") + name + ".spv"; return false; }
        const size_t n = size_t(f.tellg());
        f.seekg(0);
        std::vector<uint32_t> code((n + 3) / 4);
        f.read(reinterpret_cast<char*>(code.data()), std::streamsize(n));
        out = ctx_.createComputePipeline(code, error);
        return out != VK_NULL_HANDLE;
    };
    if (!load("LxStatic", s.staticPipe) || !load("LxDetail", s.detailPipe)) return false;

    VkDeviceContext dc{ctx_.device(), ctx_.physical(), vkGetDeviceProcAddr};
    s.iface.device = ffxGetDeviceVK(&dc);
    FfxOpticalflowContextDescription od{};
    od.backendInterface = s.iface;
    od.resolution = {width, height};
    if (FfxErrorCode e = ffxOpticalflowContextCreate(&s.of, &od); e != FFX_OK) { error = ffxErr("ffxOpticalflowContextCreate", e); return false; }
    s.ofOk = true;

    FfxFrameInterpolationContextDescription fd{};
    fd.backendInterface = s.iface;
    // Video has no depth: a constant "infinitely far" inverted depth plus display-resolution motion from optical flow.
    fd.flags = FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INVERTED | FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE |
               FFX_FRAMEINTERPOLATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
    fd.maxRenderSize = {width, height};
    fd.displaySize = {width, height};
    fd.backBufferFormat = FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
    fd.previousInterpolationSourceFormat = FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
    if (FfxErrorCode e = ffxFrameInterpolationContextCreate(&s.fi, &fd); e != FFX_OK) { error = ffxErr("ffxFrameInterpolationContextCreate", e); return false; }
    s.fiOk = true;

    if (FfxErrorCode e = s.iface.fpCreateBackendContext(&s.iface, FFX_EFFECT_SHAREDRESOURCES, nullptr, &s.sharedId); e != FFX_OK) {
        error = ffxErr("shared resource context", e);
        return false;
    }
    s.sharedOk = true;
    FfxOpticalflowSharedResourceDescriptions ofr{};
    FfxFrameInterpolationSharedResourceDescriptions fir{};
    ffxOpticalflowGetSharedResourceDescriptions(&s.of, &ofr);
    ffxFrameInterpolationGetSharedResourceDescriptions(&s.fi, &fir);
    FfxErrorCode e = FFX_OK;
    e |= s.iface.fpCreateResource(&s.iface, &ofr.opticalFlowVector, s.sharedId, &s.ofVector);
    e |= s.iface.fpCreateResource(&s.iface, &ofr.opticalFlowSCD, s.sharedId, &s.ofScd);
    e |= s.iface.fpCreateResource(&s.iface, &fir.dilatedDepth, s.sharedId, &s.dilDepth);
    e |= s.iface.fpCreateResource(&s.iface, &fir.dilatedMotionVectors, s.sharedId, &s.dilMv);
    e |= s.iface.fpCreateResource(&s.iface, &fir.reconstructedPrevNearestDepth, s.sharedId, &s.prevDepth);
    if (e != FFX_OK) { error = ffxErr("shared resource creation", e); return false; }
    s.resOk = true;
    // The backend tracks these as already usable; actually move them out of UNDEFINED so the first (reset) frame
    // does not read an image in the wrong layout.
    for (FfxResourceInternal r : {s.ofVector, s.ofScd, s.dilDepth, s.dilMv, s.prevDepth}) {
        const FfxResource res = s.iface.fpGetResource(&s.iface, r);
        if (res.description.type != FFX_RESOURCE_TYPE_BUFFER && res.resource)
            ctx_.transitionToGeneral(reinterpret_cast<VkImage>(res.resource));
    }
    s.ofW = ofr.opticalFlowVector.resourceDescription.width;
    s.ofH = ofr.opticalFlowVector.resourceDescription.height;

    const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    s.depth = ctx_.createImage(width, height, VK_FORMAT_R32_SFLOAT, u);
    s.motion = ctx_.createImage(width, height, VK_FORMAT_R16G16B16A16_SFLOAT, u);
    s.prevImg = ctx_.createImage(width, height, VK_FORMAT_R8G8B8A8_UNORM, u);
    s.stable = ctx_.createImage(width, height, ctx_.lumaFormat(), u);
    const FfxResource ov = s.iface.fpGetResource(&s.iface, s.ofVector);
    s.ofView = ctx_.wrapImage(reinterpret_cast<VkImage>(ov.resource), VK_FORMAT_R16G16_SINT, s.ofW, s.ofH);
    if (!s.depth.image || !s.motion.image || !s.ofView.view || !s.prevImg.image || !s.stable.image) { error = "Out of GPU memory for frame generation"; return false; }
    s.w = width;
    s.h = height;
    s.frameId = 0;
    return true;
}

bool FsrFrameGen::record(VkCommandBuffer cb, const ImageRes& current, const ImageRes& output, bool reset,
                         float frameTimeMs, std::string& error, bool pq, float peakNits) {
    // how the pictures are coded, so FSR judges brightness and scene changes on the right scale
    const FfxBackbufferTransferFunction transfer = pq ? FFX_BACKBUFFER_TRANSFER_FUNCTION_PQ : FFX_BACKBUFFER_TRANSFER_FUNCTION_SRGB;
    const float lumMin = pq ? 0.0001f : 0.0f, lumMax = pq ? std::clamp(peakNits, 100.0f, 10000.0f) : 1.0f;
    if (!p_ || !p_->fiOk || p_->w != current.width || p_->h != current.height) {
        if (!ensure(current.width, current.height, error)) return false;
        reset = true;
    }
    Impl& s = *p_;
    const FfxCommandList cl = ffxGetCommandListVK(cb);
    if (reset || s.frameId == 0) {
        // Constant depth (0 = infinitely far with inverted depth) and zero motion until optical flow has run.
        VkClearColorValue zero{};
        VkImageSubresourceRange rng{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, s.depth.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &rng);
        vkCmdClearColorImage(cb, s.motion.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &rng);
        ctx_.computeBarrier();
    }
    ++s.frameId;
    const FfxResource ofVec = s.iface.fpGetResource(&s.iface, s.ofVector);
    const FfxResource ofScd = s.iface.fpGetResource(&s.iface, s.ofScd);
    const FfxResource dDepth = s.iface.fpGetResource(&s.iface, s.dilDepth);
    const FfxResource dMv = s.iface.fpGetResource(&s.iface, s.dilMv);
    const FfxResource pDepth = s.iface.fpGetResource(&s.iface, s.prevDepth);
    const FfxResource cur = wrap(current, L"veyamux_current", FFX_RESOURCE_USAGE_UAV);

    // 1) optical flow (current vs. the frame it saw last time)
    FfxOpticalflowDispatchDescription od{};
    od.commandList = cl;
    od.color = cur;
    od.opticalFlowVector = ofVec;
    od.opticalFlowSCD = ofScd;
    od.reset = reset;
    od.backbufferTransferFunction = transfer;
    od.minMaxLuminance = {lumMin, lumMax};
    if (FfxErrorCode e = ffxOpticalflowContextDispatch(&s.of, &od); e != FFX_OK) { error = ffxErr("ffxOpticalflowContextDispatch", e); return false; }
    ctx_.computeBarrier();

    // 2) dense per-pixel motion (FSR's "game motion vectors"), in pixels. VeyAmux's own coarse-to-fine estimator, not
    //    the optical flow above: that one is whole-pixel per 8x8 block and reports no motion for the first ~6 frames
    //    after a reset or a detected scene change, which made frame interpolation blend two frames (double edges on
    //    every pan). FSR still gets the optical flow too, as designed.
    const ImageRes* motion = &s.motion;
    if (motionSign_ != 0.0f) {
        if (!s.estimator) s.estimator = std::make_unique<MotionEstimator>(ctx_, shaderDir_);
        motion = s.estimator->record(cb, current, reset || s.frameId <= 1, error);
        if (!motion) return false;
    }
    // static on-screen elements (HUD, text): count unchanged frames per pixel, zero their motion
    struct { uint32_t w, h, mode; float need; } sp{s.w, s.h, 0, kStaticFrames};
    const bool staticOn = protectStatic_ && s.havePrevImg && !reset;
    if (reset || !s.havePrevImg) {
        VkClearColorValue zero{};
        VkImageSubresourceRange rng{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, s.stable.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &rng);
        ctx_.computeBarrier();
    } else if (staticOn) {
        const ImageRes* in[] = {&current, &s.prevImg};
        const ImageRes* out[] = {&s.stable, motion, &output};
        ctx_.dispatch(s.staticPipe, in, 2, out, 3, &sp, sizeof sp, (s.w + 15) / 16, (s.h + 15) / 16);
        ctx_.computeBarrier();
    }

    // 3) prepare (dilate motion/depth) and interpolate
    FfxFrameInterpolationPrepareDescription pd{};
    pd.commandList = cl;
    pd.renderSize = {s.w, s.h};
    pd.jitterOffset = {0.0f, 0.0f};
    pd.motionVectorScale = {1.0f, 1.0f};       // vectors are in pixels
    pd.frameTimeDelta = frameTimeMs;
    pd.cameraNear = FLT_MAX;                    // inverted infinite depth: near is the large value
    pd.cameraFar = 0.1f;
    pd.viewSpaceToMetersFactor = 1.0f;
    pd.cameraFovAngleVertical = 1.0f;
    pd.depth = wrap(s.depth, L"veyamux_depth", FFX_RESOURCE_USAGE_READ_ONLY);
    pd.motionVectors = wrap(*motion, L"veyamux_motion", FFX_RESOURCE_USAGE_READ_ONLY);
    pd.frameID = s.frameId;
    pd.dilatedDepth = dDepth;
    pd.dilatedMotionVectors = dMv;
    pd.reconstructedPrevDepth = pDepth;
    if (FfxErrorCode e = ffxFrameInterpolationPrepare(&s.fi, &pd); e != FFX_OK) { error = ffxErr("ffxFrameInterpolationPrepare", e); return false; }

    FfxFrameInterpolationDispatchDescription fd{};
    fd.commandList = cl;
    fd.displaySize = {s.w, s.h};
    fd.renderSize = {s.w, s.h};
    fd.currentBackBuffer = cur;
    fd.output = wrap(output, L"veyamux_generated", FFX_RESOURCE_USAGE_UAV);
    fd.interpolationRect = {0, 0, int32_t(s.w), int32_t(s.h)};
    fd.opticalFlowVector = ofVec;
    fd.opticalFlowSceneChangeDetection = ofScd;
    fd.opticalFlowBufferSize = {s.ofW, s.ofH};
    fd.opticalFlowScale = {1.0f / float(s.w), 1.0f / float(s.h)};
    fd.opticalFlowBlockSize = 8;
    fd.cameraNear = pd.cameraNear;
    fd.cameraFar = pd.cameraFar;
    fd.cameraFovAngleVertical = pd.cameraFovAngleVertical;
    fd.viewSpaceToMetersFactor = 1.0f;
    fd.frameTimeDelta = frameTimeMs;
    fd.reset = reset;
    fd.backBufferTransferFunction = transfer;
    fd.minMaxLuminance[0] = lumMin;
    fd.minMaxLuminance[1] = lumMax;
    fd.frameID = s.frameId;
    fd.dilatedDepth = dDepth;
    fd.dilatedMotionVectors = dMv;
    fd.reconstructedPrevDepth = pDepth;
    if (FfxErrorCode e = ffxFrameInterpolationDispatch(&s.fi, &fd); e != FFX_OK) { error = ffxErr("ffxFrameInterpolationDispatch", e); return false; }
    ctx_.computeBarrier();
    // keep fine detail sharp where the two half-way samples disagree (otherwise FSR's average flickers against the
    // sharp real frames)
    if (keepDetail_ && s.havePrevImg && !reset) {
        static const float thr = std::getenv("VEYAMUX_FG_DETAIL_THR") ? float(std::atof(std::getenv("VEYAMUX_FG_DETAIL_THR"))) : 0.08f;
        struct { uint32_t w, h; float thr; uint32_t p0; } dp{s.w, s.h, thr, 0};
        const ImageRes* in[] = {&current, &s.prevImg};
        const ImageRes* out[] = {motion, &output};
        ctx_.dispatch(s.detailPipe, in, 2, out, 2, &dp, sizeof dp, (s.w + 15) / 16, (s.h + 15) / 16);
        ctx_.computeBarrier();
    }
    // static pixels are identical in both real frames: give the generated frame the real ones
    if (staticOn) {
        sp.mode = 1;
        const ImageRes* in[] = {&current, &s.prevImg};
        const ImageRes* out[] = {&s.stable, motion, &output};
        ctx_.dispatch(s.staticPipe, in, 2, out, 3, &sp, sizeof sp, (s.w + 15) / 16, (s.h + 15) / 16);
        ctx_.computeBarrier();
    }
    VkImageCopy c{};
    c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.extent = {s.w, s.h, 1};
    vkCmdCopyImage(cb, current.image, VK_IMAGE_LAYOUT_GENERAL, s.prevImg.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    ctx_.computeBarrier();
    s.havePrevImg = true;
    return true;
}

bool FsrFrameGen::debugCopyFlow(VkCommandBuffer cb, VkBuffer dst, uint32_t& w, uint32_t& h) {
    if (!p_ || !p_->ofView.image) return false;
    w = p_->ofW; h = p_->ofH;
    ctx_.computeBarrier();
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {w, h, 1};
    vkCmdCopyImageToBuffer(cb, p_->ofView.image, VK_IMAGE_LAYOUT_GENERAL, dst, 1, &c);
    return true;
}

} // namespace veyamux
