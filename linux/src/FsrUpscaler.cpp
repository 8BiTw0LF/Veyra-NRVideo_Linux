#include "FsrUpscaler.h"
#include "MotionEstimator.h"

#include <FidelityFX/host/backends/vk/ffx_vk.h>
#include <FidelityFX/host/ffx_fsr3upscaler.h>

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <utility>
#include <vector>

namespace veyamux {

std::mutex& ffxBackendLock() {
    static std::mutex m;
    return m;
}
namespace {
constexpr size_t kMaxContexts = 2;   // FSR 3.1 upscaler + shared resources

std::string ffxErr(const char* what, FfxErrorCode e) {
    std::string r = std::string(what) + " failed (FfxErrorCode " + std::to_string(int(e)) + ")";
    if (e == FFX_ERROR_BACKEND_API_ERROR && *ffxVeyamuxLastBackendError()) r += std::string(" at ") + ffxVeyamuxLastBackendError();
    return r;
}

FfxResource wrap(const ImageRes& img, const wchar_t* name, FfxResourceUsage usage) {
    FfxResourceDescription d{};
    d.type = FFX_RESOURCE_TYPE_TEXTURE2D;
    d.format = ffxGetSurfaceFormatVK(img.format);
    d.width = img.width;
    d.height = img.height;
    d.depth = 1;
    d.mipCount = 1;
    d.flags = FFX_RESOURCE_FLAGS_NONE;
    d.usage = usage;
    // VeyAmux images live in GENERAL, which the backend treats as the UAV state (it restores what it was given).
    return ffxGetResourceVK(reinterpret_cast<void*>(img.image), d, name, FFX_RESOURCE_STATE_UNORDERED_ACCESS);
}
} // namespace

struct FsrUpscaler::Impl {
    VkContext& ctx;
    std::vector<uint8_t> scratch;
    FfxInterface iface{};
    bool ifaceOk = false;
    FfxFsr3UpscalerContext up{};
    bool upOk = false;
    uint32_t sharedId = 0;
    bool sharedOk = false;
    FfxResourceInternal dilDepth{}, dilMv{}, prevDepth{};
    bool resOk = false;
    ImageRes depth;
    ImageRes fixedMv;                     // known/zero motion for the test hooks
    std::unique_ptr<MotionEstimator> motion;
    uint32_t inW = 0, inH = 0, outW = 0, outH = 0;
    uint64_t frames = 0;

    Impl(VkContext& c, const std::string& dir) : ctx(c), motion(std::make_unique<MotionEstimator>(c, dir)) {}
    ~Impl() { destroyAll(); }

    void destroyAll() {
        if (ctx.device()) vkDeviceWaitIdle(ctx.device());
        if (upOk) ffxFsr3UpscalerContextDestroy(&up);
        upOk = false;
        if (resOk && sharedOk)
            for (FfxResourceInternal r : {dilDepth, dilMv, prevDepth}) iface.fpDestroyResource(&iface, r, sharedId);
        resOk = false;
        if (sharedOk) iface.fpDestroyBackendContext(&iface, sharedId);
        sharedOk = false;
        ctx.destroy(depth);
        ctx.destroy(fixedMv);
        if (motion) motion->release();
        inW = inH = outW = outH = 0;
    }
};

FsrUpscaler::FsrUpscaler(VkContext& ctx, std::string shaderDir) : ctx_(ctx), shaderDir_(std::move(shaderDir)) {
    if (const char* v = std::getenv("VEYAMUX_FSR_UP_SIGN")) motionSign_ = float(std::atof(v));   // test hook
}
FsrUpscaler::~FsrUpscaler() = default;

const char* FsrUpscaler::versionString() { return "AMD FSR 3.1.6 upscaling (FidelityFX SDK 2.4.0 effect code)"; }

void FsrUpscaler::release() { p_.reset(); }

bool FsrUpscaler::ensure(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH, std::string& error) {
    if (p_ && p_->upOk && p_->inW == inW && p_->inH == inH && p_->outW == outW && p_->outH == outH) return true;
    std::lock_guard<std::mutex> guard(ffxBackendLock());
    if (!p_) p_ = std::make_unique<Impl>(ctx_, shaderDir_);
    Impl& s = *p_;
    s.destroyAll();

    if (!s.ifaceOk) {
        s.scratch.assign(ffxGetScratchMemorySizeVK(ctx_.physical(), kMaxContexts), 0);
        VkDeviceContext dc{ctx_.device(), ctx_.physical(), vkGetDeviceProcAddr};
        const FfxErrorCode e = ffxGetInterfaceVK(&s.iface, ffxGetDeviceVK(&dc), s.scratch.data(), s.scratch.size(), kMaxContexts);
        if (e != FFX_OK) { error = ffxErr("ffxGetInterfaceVK", e); return false; }
        s.ifaceOk = true;
    }
    VkDeviceContext dc{ctx_.device(), ctx_.physical(), vkGetDeviceProcAddr};
    s.iface.device = ffxGetDeviceVK(&dc);

    FfxFsr3UpscalerContextDescription ud{};
    ud.backendInterface = s.iface;
    // Video has no depth: constant "infinitely far" inverted depth; motion comes from VeyAmux's estimator at input size.
    ud.flags = FFX_FSR3UPSCALER_ENABLE_DEPTH_INVERTED | FFX_FSR3UPSCALER_ENABLE_DEPTH_INFINITE;
    ud.maxRenderSize = {inW, inH};
    ud.maxUpscaleSize = {outW, outH};
    if (FfxErrorCode e = ffxFsr3UpscalerContextCreate(&s.up, &ud); e != FFX_OK) { error = ffxErr("ffxFsr3UpscalerContextCreate", e); return false; }
    s.upOk = true;

    if (FfxErrorCode e = s.iface.fpCreateBackendContext(&s.iface, FFX_EFFECT_SHAREDRESOURCES, nullptr, &s.sharedId); e != FFX_OK) {
        error = ffxErr("shared resource context", e);
        return false;
    }
    s.sharedOk = true;
    FfxFsr3UpscalerSharedResourceDescriptions upr{};
    ffxFsr3UpscalerGetSharedResourceDescriptions(&s.up, &upr);
    FfxErrorCode e = FFX_OK;
    e |= s.iface.fpCreateResource(&s.iface, &upr.dilatedDepth, s.sharedId, &s.dilDepth);
    e |= s.iface.fpCreateResource(&s.iface, &upr.dilatedMotionVectors, s.sharedId, &s.dilMv);
    e |= s.iface.fpCreateResource(&s.iface, &upr.reconstructedPrevNearestDepth, s.sharedId, &s.prevDepth);
    if (e != FFX_OK) { error = ffxErr("shared resource creation", e); return false; }
    s.resOk = true;
    for (FfxResourceInternal r : {s.dilDepth, s.dilMv, s.prevDepth}) {
        const FfxResource res = s.iface.fpGetResource(&s.iface, r);
        if (res.description.type != FFX_RESOURCE_TYPE_BUFFER && res.resource)
            ctx_.transitionToGeneral(reinterpret_cast<VkImage>(res.resource));
    }

    const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    s.depth = ctx_.createImage(inW, inH, VK_FORMAT_R32_SFLOAT, u);
    s.fixedMv = ctx_.createImage(inW, inH, VK_FORMAT_R16G16B16A16_SFLOAT, u);
    const bool ok = s.depth.image && s.fixedMv.image;
    if (!ok) { error = "Out of GPU memory for FSR upscaling"; return false; }
    s.inW = inW; s.inH = inH; s.outW = outW; s.outH = outH;
    s.frames = 0;
    return true;
}

bool FsrUpscaler::record(VkCommandBuffer cb, const ImageRes& input, const ImageRes& output, bool reset, float frameTimeMs,
                         std::string& error) {
    if (!p_ || !p_->upOk || p_->inW != input.width || p_->inH != input.height || p_->outW != output.width || p_->outH != output.height) {
        if (!ensure(input.width, input.height, output.width, output.height, error)) { p_.reset(); return false; }
        reset = true;
    }
    Impl& s = *p_;
    const FfxCommandList cl = ffxGetCommandListVK(cb);
    if (reset || s.frames == 0) {
        VkClearColorValue zero{};
        VkImageSubresourceRange rng{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, s.depth.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &rng);
        vkCmdClearColorImage(cb, s.fixedMv.image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &rng);
        ctx_.computeBarrier();
        reset = true;
    }
    ++s.frames;

    // 1) motion between the previous input and this one (MotionEstimator: sub-pixel, coarse-to-fine)
    const ImageRes* motion = &s.fixedMv;
    static const char* constMv = std::getenv("VEYAMUX_FSR_UP_CONSTMV");   // test hook: "x,y[;x,y...]" known motion per frame (cycled)
    if (constMv && !reset) {
        VkClearColorValue v{};
        std::vector<std::pair<float, float>> seq;
        for (const char* q = constMv; *q;) {
            float x = 0, y = 0;
            if (std::sscanf(q, "%f,%f", &x, &y) == 2) seq.push_back({x, y});
            const char* sc = std::strchr(q, ';');
            if (!sc) break;
            q = sc + 1;
        }
        if (!seq.empty()) { const auto& m = seq[(s.frames - 2) % seq.size()]; v.float32[0] = m.first; v.float32[1] = m.second; }
        VkImageSubresourceRange rng{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, s.fixedMv.image, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &rng);
        ctx_.computeBarrier();
    } else if (motionSign_ != 0.0f) {
        motion = s.motion->record(cb, input, reset, error);
        if (!motion) return false;
    }

    // 3) FSR 3.1 upscale
    FfxFsr3UpscalerDispatchDescription d{};
    d.commandList = cl;
    d.color = wrap(input, L"veyamux_up_color", FFX_RESOURCE_USAGE_READ_ONLY);
    d.depth = wrap(s.depth, L"veyamux_up_depth", FFX_RESOURCE_USAGE_READ_ONLY);
    d.motionVectors = wrap(*motion, L"veyamux_up_motion", FFX_RESOURCE_USAGE_READ_ONLY);
    d.dilatedDepth = s.iface.fpGetResource(&s.iface, s.dilDepth);
    d.dilatedMotionVectors = s.iface.fpGetResource(&s.iface, s.dilMv);
    d.reconstructedPrevNearestDepth = s.iface.fpGetResource(&s.iface, s.prevDepth);
    d.output = wrap(output, L"veyamux_up_output", FFX_RESOURCE_USAGE_UAV);
    d.jitterOffset = {0.0f, 0.0f};             // video frames are not jittered; real motion supplies the sub-pixel offsets
    d.motionVectorScale = {motionSign_, motionSign_};   // vectors are in input pixels (sign: test hook, default +1)
    d.renderSize = {s.inW, s.inH};
    d.upscaleSize = {s.outW, s.outH};
    static const char* sharpEnv = std::getenv("VEYAMUX_FSR_UP_RCAS");   // test hook: RCAS strength 0..1
    const float rcas = sharpEnv ? float(std::atof(sharpEnv)) : rcas_;
    // AMD's RCAS, the sharpening pass FSR is designed to run with (FSR's accumulated output is soft without it; on
    // VeyAmux's test clips 0.9 measured best). VeyAmux's own sharpness slider still runs afterwards.
    d.enableSharpening = rcas > 0.0f;
    d.sharpness = rcas;
    d.frameTimeDelta = frameTimeMs;
    d.preExposure = 1.0f;
    d.reset = reset;
    d.cameraNear = FLT_MAX;                    // inverted infinite depth: near is the large value
    d.cameraFar = 0.1f;
    d.cameraFovAngleVertical = 1.0f;
    d.viewSpaceToMetersFactor = 1.0f;
    d.flags = std::getenv("VEYAMUX_FSR_UP_DEBUGVIEW") ? FFX_FSR3UPSCALER_DISPATCH_DRAW_DEBUG_VIEW : 0;   // test hook
    if (motionSign_ == 0.0f) d.motionVectorScale = {1.0f, 1.0f};
    if (FfxErrorCode e = ffxFsr3UpscalerContextDispatch(&s.up, &d); e != FFX_OK) { error = ffxErr("ffxFsr3UpscalerContextDispatch", e); return false; }
    ctx_.computeBarrier();
    return true;
}

bool FsrUpscaler::debugCopyMotion(VkCommandBuffer cb, VkBuffer dst, int which) {
    (void)which;
    if (!p_ || !p_->motion || !p_->motion->field().image) return false;
    const ImageRes& img = p_->motion->field();
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {img.width, img.height, 1};
    vkCmdCopyImageToBuffer(cb, img.image, VK_IMAGE_LAYOUT_GENERAL, dst, 1, &c);
    return true;
}

} // namespace veyamux
