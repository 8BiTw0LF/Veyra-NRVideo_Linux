#include "EnhanceChain.h"
#include "../third_party/nis/NIS_Config.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace veyamux {
namespace {
constexpr VkFormat kLinearFmt = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr VkImageUsageFlags kWork = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
uint32_t groups(int v, int g) { return uint32_t((v + g - 1) / g); }

// cbuffer mirrors (DX packing, see the HLSL files)
struct RgbParams {
    uint32_t width, height, transfer, limited;
    float reserved[2]; float primaries2020, padding;
    float colorRow0[4], colorRow1[4], colorRow2[4], colorControls[4], colorFlags[4];
};
static_assert(sizeof(RgbParams) == 112);
struct AaParams {
    uint32_t width, height; float invW, invH;
    float workingToWhite, threshold, strength, historyWeight;
    float subsample[4];
    float historyRejection, historyValid, r0, r1;
};
static_assert(sizeof(AaParams) == 64);
struct ScaleParams { uint32_t sw, sh, ow, oh; float encodeSrgb, ditherStep, p2, p3; };
struct LanczosParams { uint32_t sw, sh, dw, dh; };
struct EnhanceParams { uint32_t w, h; float sharpen, exposure, contrast, saturation, pad[2]; };
} // namespace

EnhanceChain::EnhanceChain(VkContext& ctx, std::string shaderDir)
    : ctx_(ctx), dir_(std::move(shaderDir)), fsrUp_(std::make_unique<FsrUpscaler>(ctx, dir_)) {}

EnhanceChain::~EnhanceChain() {
    liveMode_ = false;
    if (ctx_.device()) vkDeviceWaitIdle(ctx_.device());
    timer_.destroy();
    releaseStream();
    fsrUp_.reset();
    fg_.reset();
    ctx_.destroy(nisScale_);
    ctx_.destroy(nisUsm_);
    ctx_.destroy(nisStaging_);
    ctx_.destroy(fgOut_);
    ctx_.destroy(disp_);
    nrMotion_.reset();
    releaseResources();
    for (auto& r : retired_) ctx_.destroy(r.img);
    retired_.clear();
    for (auto& [n, p] : kernels_) ctx_.destroy(p);
}

bool EnhanceChain::loadKernel(const std::string& name, std::string& error) {
    const std::string path = dir_ + "/" + name + ".spv";
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) { error = "Missing shader: " + path; return false; }
    const auto size = size_t(f.tellg());
    f.seekg(0);
    std::vector<uint32_t> code((size + 3) / 4);
    f.read(reinterpret_cast<char*>(code.data()), std::streamsize(size));
    std::string e;
    VkPipeline p = ctx_.createComputePipeline(code, e);
    if (!p) { error = name + ": " + e; return false; }
    kernels_[name] = p;
    return true;
}

bool EnhanceChain::init(std::string& error) {
    for (const char* n : {"RgbToLinear", "AaLuma", "AaFxaa3", "AaFxaa4", "AaFxaa5", "LxLanczos", "LxEnhance", "ScaleBlit", "LxYuv", "LxDownscale", "LxNlm", "LxTemporalNr", "LxP010", "LxFsr1", "LxHdrRgb", "LxNv12", "LxPqOut", "LxNis", "LxCrtGeom", "LxPixelGrid", "LxDeflicker"})
        if (!loadKernel(n, error)) return false;
    timer_.init(ctx_.physical(), ctx_.device(), ctx_.queueFamily());   // optional: no timestamps = no GPU time shown
    return true;
}

VkPipeline EnhanceChain::kernel(const char* name) const {
    auto it = kernels_.find(name);
    return it == kernels_.end() ? VK_NULL_HANDLE : it->second;
}

void EnhanceChain::outputSize(int w, int h, const EnhanceSettings& s, int& ow, int& oh) {
    if (s.pixelActive()) {
        // the game's grid when it is measured and smaller than the signal, else the signal itself
        const bool grid = s.pixelGrid == 1 && s.gridW >= 64 && s.gridH >= 64 && s.gridW < w && s.gridH < h;
        ow = grid ? (s.gridW & ~1) : w;
        oh = grid ? (s.gridH & ~1) : h;
        return;
    }
    if (!s.enabled || s.scale <= 1.001f) { ow = w; oh = h; return; }
    ow = std::max(2, int(std::lround(w * s.scale)) & ~1);
    oh = std::max(2, int(std::lround(h * s.scale)) & ~1);
}

void EnhanceChain::releaseResources() {
    for (ImageRes* i : {&src_, &lin_, &linB_, &luma_, &up_, &fin_, &rawPacked_, &rawY_, &rawUV_, &rawY16_, &rawUV16_, &src16_, &nrOut_, &nrHist_[0], &nrHist_[1], &dfOut_, &dfPrev_[0], &dfPrev_[1], &dfAl_[0], &dfAl_[1], &dfPrev2_[0], &dfPrev2_[1]}) ctx_.destroy(*i);
    nrHistory_ = false;
    dfHistory_ = false;
    if (liveMode_) retire(out8_); else ctx_.destroy(out8_);
    for (auto& b : liveStaging_) ctx_.destroy(b);
    liveStagingSize_ = 0;
    ctx_.destroy(staging_);
    ctx_.destroy(staging16_);
    ctx_.destroy(readback_);
    w_ = h_ = ow_ = oh_ = 0;
}

bool EnhanceChain::ensureResources(int w, int h, int ow, int oh, std::string& error) {
    if (w == w_ && h == h_ && ow == ow_ && oh == oh_ && src_.image) return true;
    vkDeviceWaitIdle(ctx_.device());
    releaseResources();
    const VkImageUsageFlags upl = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    src_ = ctx_.createImage(w, h, VK_FORMAT_R8G8B8A8_UNORM, upl);
    lin_ = ctx_.createImage(w, h, kLinearFmt, kWork);
    linB_ = ctx_.createImage(w, h, kLinearFmt, kWork);
    luma_ = ctx_.createImage(w, h, ctx_.lumaFormat(), kWork);
    up_ = ctx_.createImage(ow, oh, kLinearFmt, kWork);
    fin_ = ctx_.createImage(ow, oh, kLinearFmt, kWork);
    out8_ = ctx_.createImage(ow, oh, VK_FORMAT_R8G8B8A8_UNORM,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    ++liveGen_;
    staging_ = ctx_.createBuffer(VkDeviceSize(w) * h * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
    readback_ = ctx_.createBuffer(VkDeviceSize(ow) * oh * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
    if (!src_.image || !lin_.image || !linB_.image || !luma_.image || !up_.image || !fin_.image || !out8_.image ||
        !staging_.buffer || !readback_.buffer) {
        error = "Out of GPU memory while allocating the enhancement chain";
        releaseResources();
        return false;
    }
    w_ = w; h_ = h; ow_ = ow; oh_ = oh;
    return true;
}

bool EnhanceChain::process(const uint8_t* rgba, int w, int h, int stride, const EnhanceSettings& s,
                           std::vector<uint8_t>& out, int& outW, int& outH, std::string& error,
                           float frameMs, int hdr) {
    int ow, oh;
    outputSize(w, h, s, ow, oh);
    if (!ensureResources(w, h, ow, oh, error)) return false;
    outW = ow; outH = oh;
    frameMs_ = frameMs;
    if (hdr && (!src16_.image || int(src16_.width) != w || int(src16_.height) != h)) {   // HDR video: 16-bit input
        if (src16_.image) { vkDeviceWaitIdle(ctx_.device()); ctx_.destroy(src16_); ctx_.destroy(staging16_); }
        src16_ = ctx_.createImage(uint32_t(w), uint32_t(h), VK_FORMAT_R16G16B16A16_UNORM,
                                  VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        staging16_ = ctx_.createBuffer(VkDeviceSize(w) * h * 8, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
        if (!src16_.image || !staging16_.mapped) { error = "Out of GPU memory for HDR video"; return false; }
    }

    // Upload (row by row so any source stride works).
    const size_t rowBytes = size_t(w) * (hdr ? 8 : 4);
    uint8_t* stage = static_cast<uint8_t*>(hdr ? staging16_.mapped : staging_.mapped);
    for (int y = 0; y < h; ++y) std::memcpy(stage + size_t(y) * rowBytes, rgba + size_t(y) * stride, rowBytes);

    ctx_.beginFrame();
    VkCommandBuffer cmd = ctx_.cmd();
    timer_.begin(cmd, 0);
    VkBufferImageCopy up{};
    up.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    up.imageExtent = {uint32_t(w), uint32_t(h), 1};
    vkCmdCopyBufferToImage(cmd, hdr ? staging16_.buffer : staging_.buffer, hdr ? src16_.image : src_.image,
                           VK_IMAGE_LAYOUT_GENERAL, 1, &up);
    ctx_.computeBarrier();

    if (hdr) {
        const float white = hdrTarget(s);
        frameNits_ = white;
        frame2020_ = s.hdrOut;
        struct { uint32_t w, h, mode, wide; float peak, white, p1, p2; } hp{uint32_t(w), uint32_t(h), uint32_t(hdr), s.hdrOut ? 1u : 0u,
                                                                         std::clamp(s.hdrPeak, 203.0f, 10000.0f), white, 0, 0};
        run("LxHdrRgb", {&src16_}, {&lin_}, &hp, sizeof hp, w, h, 16);
    } else {
        frameNits_ = 203.0f;
        frame2020_ = false;
        RgbParams rp{};
        rp.width = w; rp.height = h; rp.transfer = 1; rp.limited = 0;   // sws already produced full-range sRGB-coded RGB
        run("RgbToLinear", {&src_}, {&lin_}, &rp, sizeof rp, w, h, 16);
    }
    timer_.mark(cmd, GpuTimer::Input);

    auto post = [&](const ImageRes* start, BufferRes& dstBuf) {
        recordPost(start, s);
        VkBufferImageCopy rb{};
        rb.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        rb.imageExtent = {uint32_t(ow), uint32_t(oh), 1};
        vkCmdCopyImageToBuffer(cmd, out8_.image, VK_IMAGE_LAYOUT_GENERAL, dstBuf.buffer, 1, &rb);
        ctx_.computeBarrier();
        timer_.mark(cmd, GpuTimer::Output);
    };
    post(&lin_, readback_);

    VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);

    if (!ctx_.submitAndWait(error)) return false;
    timer_.collectNow(0);
    out.resize(size_t(ow) * oh * 4);
    std::memcpy(out.data(), readback_.mapped, out.size());
    return true;
}


// ------------------------------------------------------------------ shared recording helpers

void EnhanceChain::run(const char* k, std::initializer_list<const ImageRes*> in, std::initializer_list<const ImageRes*> outs,
                       const void* ubo, uint32_t uboSize, int dw, int dh, int g) {
    std::vector<const ImageRes*> a(in), b(outs);
    ctx_.dispatch(kernel(k), a.data(), uint32_t(a.size()), b.data(), uint32_t(b.size()), ubo, uboSize, groups(dw, g),
                  groups(dh, g));
    ctx_.computeBarrier();
}

// [FXAA] -> [Lanczos] -> [sharpen/grade] -> sRGB encode into out8_.
void EnhanceChain::recordPost(const ImageRes* start, const EnhanceSettings& s) {
    const int w = w_, h = h_, ow = ow_, oh = oh_;
    const ImageRes* cur = start;
    const bool temporalNr = s.enabled && (s.denoise == Denoise::Temporal || s.denoise == Denoise::Both);
    const bool deflicker = s.enabled && s.deflicker;
    // one motion estimate per frame for anti-flicker and the temporal denoiser (both look at the source)
    const ImageRes* mv = (temporalNr || deflicker) ? recordMotion(start) : nullptr;
    if (deflicker) cur = recordDeflicker(cur, mv);
    else dfHistory_ = false;
    if (s.enabled && s.denoise != Denoise::Off) { cur = recordDenoise(cur, s, mv); timer_.mark(ctx_.active(), GpuTimer::Denoise); }
    else nrHistory_ = false;
    if (s.enabled && s.fxaa) {
        AaParams ap{};
        ap.width = w; ap.height = h; ap.invW = 1.0f / w; ap.invH = 1.0f / h; ap.workingToWhite = 1.0f;
        ap.threshold = 0.1f; ap.strength = 1.0f;
        run("AaLuma", {cur}, {&luma_}, &ap, sizeof ap, w, h, 8);
        const char* fx = s.fxaaQuality <= 0 ? "AaFxaa3" : (s.fxaaQuality == 1 ? "AaFxaa4" : "AaFxaa5");
        run(fx, {cur, &luma_}, {&linB_}, &ap, sizeof ap, w, h, 8);
        cur = &linB_;
        timer_.mark(ctx_.active(), GpuTimer::AntiAlias);
    }
    if (s.retroTv) {
        // Retro TV: CRT-Geom draws the source straight at the output size (it does its own scaling, so it replaces the
        // upscaler; sharpening after it would only sharpen the scanlines)
        struct { uint32_t sw, sh, ow, oh, frame; float simW, simH; uint32_t p0; } cp{
            uint32_t(w), uint32_t(h), uint32_t(ow), uint32_t(oh), uint32_t(++crtFrame_), 0.0f,
            s.retroLines > 0 ? float(std::min(s.retroLines, h)) : 0.0f, 0};
        run("LxCrtGeom", {cur}, {&up_}, &cp, sizeof cp, ow, oh, 16);
        cur = &up_;
        upHistory_ = false;
        timer_.mark(ctx_.active(), GpuTimer::Upscale);
    } else if (s.pixelActive()) {
        // pixel-perfect: back to the game's grid (if measured); the display enlarges it with sharp pixels
        if (ow != w || oh != h) {
            struct { uint32_t sw, sh, ow, oh; } gp{uint32_t(w), uint32_t(h), uint32_t(ow), uint32_t(oh)};
            run("LxPixelGrid", {cur}, {&up_}, &gp, sizeof gp, ow, oh, 16);
            cur = &up_;
        }
        upHistory_ = false;
        timer_.mark(ctx_.active(), GpuTimer::Upscale);
    } else if (s.enabled && (ow != w || oh != h || s.fsrNativeAa())) {
        bool done = false;
        // A setup failure is not retried every frame (context creation stalls the GPU): only after the size or the
        // upscaler choice changes.
        const uint64_t key = (uint64_t(w) << 48) ^ (uint64_t(h) << 32) ^ (uint64_t(ow) << 16) ^ uint64_t(oh);
        if (s.upscaler == Upscaler::AmdFsr3 && key != upFailedKey_) {
            std::string e;
            done = fsrUp_->record(ctx_.active(), *cur, up_, !upHistory_, frameMs_, e);
            upHistory_ = done;
            if (!done) { upError_ = e; upFailedKey_ = key; }
            else upError_.clear();
        } else if (s.upscaler != Upscaler::AmdFsr3) {
            upHistory_ = false;
            upError_.clear();
            upFailedKey_ = 0;
        }
        // FSR 3.1 native AA that could not start: the picture stays as it is
        const bool skip = !done && ow == w && oh == h;
        if (skip) done = true;
        if (!done && s.upscaler == Upscaler::Nis) done = recordNis(*cur, w, h, ow, oh);
        if (!done && s.upscaler == Upscaler::AmdFsr1) {
            // AMD FSR 1: EASU into fin_ (free until the finishing pass), RCAS back into up_
            struct { uint32_t iw, ih, ow, oh, mode; float sharp; uint32_t p0, p1; } fp{uint32_t(w), uint32_t(h), uint32_t(ow), uint32_t(oh), 0, 0, 0, 0};
            static const bool noRcas = std::getenv("VEYAMUX_FSR1_NORCAS") != nullptr;   // test hook
            run("LxFsr1", {cur}, {noRcas ? &up_ : &fin_}, &fp, sizeof fp, ow, oh, 16);
            if (noRcas) { cur = &up_; }
            // RCAS strength in stops below its maximum (AMD suggests 0.2 - 0.5); VEYAMUX_FSR1_RCAS overrides (tests)
            static const float rcasStops = std::getenv("VEYAMUX_FSR1_RCAS") ? float(std::atof(std::getenv("VEYAMUX_FSR1_RCAS"))) : 0.5f;
            fp.mode = 1; fp.sharp = rcasStops;
            if (!noRcas) run("LxFsr1", {&fin_}, {&up_}, &fp, sizeof fp, ow, oh, 16);
            done = true;
        }
        if (!done) {
            LanczosParams lp{uint32_t(w), uint32_t(h), uint32_t(ow), uint32_t(oh)};
            run("LxLanczos", {cur}, {&up_}, &lp, sizeof lp, ow, oh, 16);
        }
        if (!skip) cur = &up_;
        timer_.mark(ctx_.active(), GpuTimer::Upscale);
    } else {
        upHistory_ = false;
    }
    if (s.enabled && ((s.sharpen > 0.001f && !s.retroTv && !s.pixelActive()) || s.exposure != 0.0f || s.contrast != 1.0f || s.saturation != 1.0f)) {
        EnhanceParams ep{uint32_t(ow), uint32_t(oh), (s.retroTv || s.pixelActive()) ? 0.0f : s.sharpen, s.exposure, s.contrast, s.saturation, {0, 0}};
        run("LxEnhance", {cur}, {&fin_}, &ep, sizeof ep, ow, oh, 16);
        cur = &fin_;
    }
    outPq_ = s.hdrOut;
    if (s.hdrOut) {   // HDR display: PQ-coded light (1.0 in the working image = frameNits_ nits)
        struct { uint32_t w, h; float nits; uint32_t frame, wide, p0, p1, p2; } pp{uint32_t(ow), uint32_t(oh), frameNits_, uint32_t(++pqFrame_),
                                                                             frame2020_ ? 1u : 0u, 0, 0, 0};
        run("LxPqOut", {cur}, {&out8_}, &pp, sizeof pp, ow, oh, 16);
    } else {
        ScaleParams sp{uint32_t(ow), uint32_t(oh), uint32_t(ow), uint32_t(oh), 1.0f, 0.0f, 0, 0};
        run("ScaleBlit", {cur}, {&out8_}, &sp, sizeof sp, ow, oh, 16);
    }
    timer_.mark(ctx_.active(), GpuTimer::Finish);
}

// Noise levels (perceptual units, sqrt of linear light) for light / medium / strong.
static constexpr float kNrSigma[3] = {0.010f, 0.018f, 0.028f};
static constexpr float kNrBlend[3] = {0.6f, 0.8f, 0.9f};   // temporal: at most this much history per frame

const ImageRes* EnhanceChain::recordMotion(const ImageRes* in) {
    if (!nrMotion_) nrMotion_ = std::make_unique<MotionEstimator>(ctx_, dir_);
    std::string e;
    static const bool zeroMv = std::getenv("VEYAMUX_NR_ZEROMV") != nullptr;   // test hook: no motion
    // a reset when neither temporal pass has history (a new sequence)
    return nrMotion_->record(ctx_.active(), *in, (!nrHistory_ && !dfHistory_) || zeroMv, e);
}

// Anti-flicker (LxDeflicker): keeps the previous source frame and the one before it (aligned to the previous frame).
const ImageRes* EnhanceChain::recordDeflicker(const ImageRes* in, const ImageRes* mv) {
    const int w = w_, h = h_;
    if (!dfOut_.image) {
        dfOut_ = ctx_.createImage(w, h, kLinearFmt, kWork);
        for (int i = 0; i < 2; ++i) {
            dfPrev_[i] = ctx_.createImage(w, h, kLinearFmt, kWork);
            dfAl_[i] = ctx_.createImage(w, h, kLinearFmt, kWork);
            dfPrev2_[i] = ctx_.createImage(w, h, kLinearFmt, kWork);
        }
        dfHistory_ = false;
        if (!dfOut_.image || !dfPrev_[0].image || !dfPrev_[1].image || !dfAl_[0].image || !dfAl_[1].image || !dfPrev2_[0].image || !dfPrev2_[1].image) return in;
    }
    static const float thr = std::getenv("VEYAMUX_DF_THRESHOLD") ? float(std::atof(std::getenv("VEYAMUX_DF_THRESHOLD"))) : 0.02f;
    const int prev = dfCur_;
    dfCur_ ^= 1;
    struct { uint32_t w, h; float threshold, strength; uint32_t history, p0, p1, p2; } dp{
        uint32_t(w), uint32_t(h), thr, 1.0f, (dfHistory_ && mv) ? 1u : 0u, 0, 0, 0};
    run("LxDeflicker", {in, &dfPrev_[prev], &dfAl_[prev], mv ? mv : in, &dfPrev2_[prev]},
        {&dfOut_, &dfPrev_[dfCur_], &dfAl_[dfCur_], &dfPrev2_[dfCur_]}, &dp, sizeof dp, w, h, 16);
    dfHistory_ = true;
    return &dfOut_;
}

const ImageRes* EnhanceChain::recordDenoise(const ImageRes* in, const EnhanceSettings& s, const ImageRes* mv) {
    const int w = w_, h = h_;
    if (!nrOut_.image) {
        nrOut_ = ctx_.createImage(w, h, kLinearFmt, kWork);
        nrHist_[0] = ctx_.createImage(w, h, kLinearFmt, kWork);
        nrHist_[1] = ctx_.createImage(w, h, kLinearFmt, kWork);
        nrHistory_ = false;
        if (!nrOut_.image || !nrHist_[0].image || !nrHist_[1].image) return in;
    }
    const int k = std::clamp(s.denoiseStrength, 0, 2);
    const ImageRes* cur = in;
    if (s.denoise == Denoise::Temporal || s.denoise == Denoise::Both) {
        if (mv) {
            const int prev = nrCur_;
            nrCur_ ^= 1;
            struct { uint32_t w, h; float sigma, blend; uint32_t reset, p0, p1, p2; } tp{
                uint32_t(w), uint32_t(h), kNrSigma[k], kNrBlend[k], nrHistory_ ? 0u : 1u, 0, 0, 0};
            run("LxTemporalNr", {in, &nrHist_[prev], mv}, {&nrHist_[nrCur_]}, &tp, sizeof tp, w, h, 16);
            cur = &nrHist_[nrCur_];
            nrHistory_ = true;
        } else {
            nrHistory_ = false;
        }
    } else {
        nrHistory_ = false;
    }
    if (s.denoise == Denoise::Spatial || s.denoise == Denoise::Both) {
        // after the temporal pass less noise is left: a gentler spatial pass keeps more texture
        static const float tuneS = std::getenv("VEYAMUX_NR_SIGMA") ? float(std::atof(std::getenv("VEYAMUX_NR_SIGMA"))) : 0.0f;
        static const float tuneH = std::getenv("VEYAMUX_NR_H") ? float(std::atof(std::getenv("VEYAMUX_NR_H"))) : 0.7f;
        const float sg = (tuneS > 0 ? tuneS : kNrSigma[k]) * (s.denoise == Denoise::Both ? 0.6f : 1.0f);
        struct { uint32_t w, h; float sigma, filterH; } np{uint32_t(w), uint32_t(h), sg, sg * tuneH};
        run("LxNlm", {cur}, {&nrOut_}, &np, sizeof np, w, h, 16);
        cur = &nrOut_;
    }
    return cur;
}

void EnhanceChain::retire(ImageRes& img) {
    if (img.image) retired_.push_back({img, 8});
    img = {};
}

void EnhanceChain::collectRetired() {
    for (auto it = retired_.begin(); it != retired_.end();) {
        if (--it->framesLeft <= 0) { ctx_.destroy(it->img); it = retired_.erase(it); }
        else ++it;
    }
}

bool EnhanceChain::ensureLive(int w, int h, int ow, int oh, std::string& error) {
    const VkDeviceSize need = VkDeviceSize(w) * h * 4;
    if (liveW_ == w && liveH_ == h && w_ == w && h_ == h && ow_ == ow && oh_ == oh && out8_.image && liveStagingSize_ >= need)
        return true;
    liveMode_ = true;
    if (!ensureResources(w, h, ow, oh, error)) return false;     // also recreates out8_ (old one retired)
    rawPacked_ = ctx_.createImage(uint32_t(w / 2), uint32_t(h), VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    rawY_ = ctx_.createImage(uint32_t(w), uint32_t(h), VK_FORMAT_R8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    rawUV_ = ctx_.createImage(uint32_t(w / 2), uint32_t(h / 2), VK_FORMAT_R8G8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    bool ok = rawPacked_.image && rawY_.image && rawUV_.image;
    for (auto& b : liveStaging_) {
        b = ctx_.createBuffer(need, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
        ok = ok && b.buffer;
    }
    if (!ok) { error = "Out of GPU memory while allocating the live capture path"; return false; }
    liveStagingSize_ = need;
    liveW_ = w; liveH_ = h;
    return true;
}

bool EnhanceChain::recordLive(VkCommandBuffer cb, int slot, const LiveInput& in, const EnhanceSettings& s, std::string& error,
                              float frameMs) {
    frameMs_ = frameMs;
    int ow, oh;
    outputSize(in.w, in.h, s, ow, oh);
    if (!ensureLive(in.w, in.h, ow, oh, error)) return false;
    collectRetired();
    if (in.fmt == LiveInput::P010 && !rawY16_.image) {   // HDR capture: 16-bit planes, created on first use
        const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        rawY16_ = ctx_.createImage(uint32_t(in.w), uint32_t(in.h), VK_FORMAT_R16_UNORM, u);
        rawUV16_ = ctx_.createImage(uint32_t(in.w / 2), uint32_t(in.h / 2), VK_FORMAT_R16G16_UNORM, u);
        if (!rawY16_.image || !rawUV16_.image) { error = "Out of GPU memory for HDR capture"; return false; }
    }
    const int sl = ((slot % VkContext::kSlots) + VkContext::kSlots) % VkContext::kSlots;
    const size_t bytes = std::min<size_t>(in.size, size_t(liveStagingSize_));
    std::memcpy(liveStaging_[sl].mapped, in.data, bytes);

    ctx_.beginExternal(cb, sl);
    timer_.begin(cb, sl);
    // Previous frames may still be reading our images (Qt's fragment pass) or the staging buffer: wait for them.
    {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask = mb.srcAccessMask;
        // The image Qt displays is handed to Qt in SHADER_READ_ONLY_OPTIMAL and Qt leaves it there. It is completely
        // rewritten every frame, so take it back to GENERAL with UNDEFINED as the old layout (contents discarded) -
        // valid whatever layout Qt left it in.
        VkImageMemoryBarrier ib{};
        ib.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        ib.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        ib.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ib.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib.image = out8_.image;
        ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0, 1, &mb, 0, nullptr, 1, &ib);
    }
    const BufferRes& st = liveStaging_[sl];
    auto copyTo = [&](const ImageRes& img, VkDeviceSize offset, uint32_t rowLenTexels, uint32_t w, uint32_t h) {
        VkBufferImageCopy c{};
        c.bufferOffset = offset;
        c.bufferRowLength = rowLenTexels;
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {w, h, 1};
        vkCmdCopyBufferToImage(cb, st.buffer, img.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    };
    const uint32_t W = uint32_t(in.w), H = uint32_t(in.h);
    switch (in.fmt) {
    case LiveInput::RGBA: copyTo(src_, 0, uint32_t(in.stride / 4), W, H); break;
    case LiveInput::YUYV:
    case LiveInput::UYVY: copyTo(rawPacked_, 0, uint32_t(in.stride / 4), W / 2, H); break;
    case LiveInput::NV12:
        copyTo(rawY_, 0, uint32_t(in.stride), W, H);
        copyTo(rawUV_, VkDeviceSize(in.stride) * H, uint32_t(in.stride / 2), W / 2, H / 2);
        break;
    case LiveInput::P010:
        copyTo(rawY16_, 0, uint32_t(in.stride / 2), W, H);
        copyTo(rawUV16_, VkDeviceSize(in.stride) * H, uint32_t(in.stride / 4), W / 2, H / 2);
        break;
    }
    ctx_.computeBarrier();
    if (in.fmt == LiveInput::P010) {
        // HDR: PQ/BT.2020 -> tone-mapped linear SDR straight into the working image (keeps all 10 bits)
        const float white = hdrTarget(s);
        frameNits_ = white;
        frame2020_ = s.hdrOut;
        struct { uint32_t w, h, full, wide; float peak, white, p1, p2; } hp{W, H, in.fullRange ? 1u : 0u, s.hdrOut ? 1u : 0u,
                                                                         std::clamp(s.hdrPeak, 203.0f, 10000.0f), white, 0, 0};
        run("LxP010", {&rawY16_, &rawUV16_}, {&lin_}, &hp, sizeof hp, in.w, in.h, 16);
    } else if (in.fmt != LiveInput::RGBA) {
        struct { uint32_t w, h, mode, full, bt709, p0, p1, p2; } yp{W, H, in.fmt == LiveInput::YUYV ? 0u : (in.fmt == LiveInput::UYVY ? 1u : 2u),
                                                                      in.fullRange ? 1u : 0u, (in.h >= 720 || in.w >= 1280) ? 1u : 0u, 0, 0, 0};
        run("LxYuv", {&rawPacked_, &rawY_, &rawUV_}, {&src_}, &yp, sizeof yp, in.w, in.h, 16);
    }
    if (in.fmt != LiveInput::P010) {
        frameNits_ = 203.0f;
        frame2020_ = false;
        RgbParams rp{};
        rp.width = W; rp.height = H; rp.transfer = 1; rp.limited = 0;
        run("RgbToLinear", {&src_}, {&lin_}, &rp, sizeof rp, in.w, in.h, 16);
    }
    timer_.mark(cb, GpuTimer::Input);
    recordPost(&lin_, s);
    // OBS stream copy of the displayed image
    if (streamMaxH_.load() >= 0) { recordStream(cb, out8_, 0); timer_.mark(cb, GpuTimer::Output); }
    if (recMaxH_.load() >= 0) recordRec(cb, out8_, in.time);
    if (shotWanted_.exchange(false)) recordShot(cb, out8_);

    // FSR frame generation: the generated frame goes to the display image now, the real one half a frame later.
    if (s.fsrFg()) {
        if (recordFg(cb)) return true;
    } else if (fgActive_) {
        fgActive_ = fgRealPending_ = false;   // back to showing out8_ directly
        fgReset_ = true;
        ++liveGen_;
    }
    // Hand the display image to Qt's fragment shader in SHADER_READ_ONLY_OPTIMAL - the layout the QSGTexture
    // wrapper is created with (VideoItem), so Qt's own layout tracking matches reality.
    VkImageMemoryBarrier ob{};
    ob.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    ob.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    ob.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    ob.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    ob.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    ob.srcQueueFamilyIndex = ob.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ob.image = out8_.image;
    ob.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ob);
    return true;
}

// ------------------------------------------------------------------ NVIDIA Image Scaling

bool EnhanceChain::recordNis(const ImageRes& src, int w, int h, int ow, int oh) {
    const VkCommandBuffer cb = ctx_.active();
    if (!nisScale_.image) {   // coefficient tables, uploaded once
        const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        nisScale_ = ctx_.createImage(uint32_t(kFilterSize / 4), uint32_t(kPhaseCount), VK_FORMAT_R32G32B32A32_SFLOAT, u);
        nisUsm_ = ctx_.createImage(uint32_t(kFilterSize / 4), uint32_t(kPhaseCount), VK_FORMAT_R32G32B32A32_SFLOAT, u);
        nisStaging_ = ctx_.createBuffer(sizeof coef_scale + sizeof coef_usm, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, false);
        if (!nisScale_.image || !nisUsm_.image || !nisStaging_.buffer || !nisStaging_.mapped) return false;
        ctx_.transitionToGeneral(nisScale_.image);
        ctx_.transitionToGeneral(nisUsm_.image);
        std::memcpy(nisStaging_.mapped, coef_scale, sizeof coef_scale);
        std::memcpy(static_cast<char*>(nisStaging_.mapped) + sizeof coef_scale, coef_usm, sizeof coef_usm);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {uint32_t(kFilterSize / 4), uint32_t(kPhaseCount), 1};
        vkCmdCopyBufferToImage(cb, nisStaging_.buffer, nisScale_.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        c.bufferOffset = sizeof coef_scale;
        vkCmdCopyBufferToImage(cb, nisStaging_.buffer, nisUsm_.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        ctx_.computeBarrier();
    }
    NISConfig cfg{};
    // NIS sharpness 0..1 (NVIDIA's default 0.5); VeyAmux's own Sharpness slider still applies afterwards
    static const float sharp = std::getenv("VEYAMUX_NIS_SHARP") ? float(std::atof(std::getenv("VEYAMUX_NIS_SHARP"))) : 0.4f;
    if (!NVScalerUpdateConfig(cfg, sharp, 0, 0, uint32_t(w), uint32_t(h), uint32_t(w), uint32_t(h),
                              0, 0, uint32_t(ow), uint32_t(oh), uint32_t(ow), uint32_t(oh), NISHDRMode::Linear))
        return false;
    const ImageRes* in[] = {&src, &nisScale_, &nisUsm_};
    const ImageRes* out[] = {&up_};
    ctx_.dispatch(kernel("LxNis"), in, 3, out, 1, &cfg, sizeof cfg, uint32_t((ow + 31) / 32), uint32_t((oh + 23) / 24));
    ctx_.computeBarrier();
    return true;
}

// ------------------------------------------------------------------ FSR 3.1 frame generation (live)

void EnhanceChain::copyToDisplay(VkCommandBuffer cb, const ImageRes& src) {
    VkImageMemoryBarrier b[2]{};
    for (auto& x : b) {
        x.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        x.srcQueueFamilyIndex = x.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        x.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    }
    // display image: whatever Qt left it in -> GENERAL (fully overwritten); source: finished writes -> transfer read
    b[0].image = disp_.image;
    b[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    b[0].dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    b[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    b[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    b[1].image = src.image;
    b[1].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    b[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    b[1].oldLayout = b[1].newLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 2, b);
    VkImageCopy c{};
    c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.extent = {disp_.width, disp_.height, 1};
    vkCmdCopyImage(cb, src.image, VK_IMAGE_LAYOUT_GENERAL, disp_.image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    VkImageMemoryBarrier o{};
    o.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    o.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    o.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    o.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    o.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    o.srcQueueFamilyIndex = o.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    o.image = disp_.image;
    o.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &o);
}

bool EnhanceChain::recordFg(VkCommandBuffer cb) {
    if (disp_.width != out8_.width || disp_.height != out8_.height || !disp_.image) {
        retire(fgOut_);   // Qt may still be drawing the old display image: freed a few frames later
        retire(disp_);
        fgOut_ = ImageRes{};
        disp_ = ImageRes{};
        const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        fgOut_ = ctx_.createImage(out8_.width, out8_.height, VK_FORMAT_R8G8B8A8_UNORM, u);
        disp_ = ctx_.createImage(out8_.width, out8_.height, VK_FORMAT_R8G8B8A8_UNORM, u);
        if (!fgOut_.image || !disp_.image) { fgError_ = "Out of GPU memory for frame generation"; return false; }
        ctx_.transitionToGeneral(fgOut_.image);
        fgReset_ = true;
        ++liveGen_;
    }
    if (!fgActive_) { fgActive_ = true; fgReset_ = true; ++liveGen_; }
    if (!fg_) fg_ = std::make_unique<FsrFrameGen>(ctx_, dir_);
    ctx_.computeBarrier();
    const bool reset = fgReset_;
    std::string err;
    if (!fg_->record(cb, out8_, fgOut_, reset, frameMs_, err)) {
        fgError_ = err;
        fgActive_ = fgRealPending_ = false;
        ++liveGen_;
        return false;
    }
    fgError_.clear();
    fgReset_ = false;
    copyToDisplay(cb, reset ? out8_ : fgOut_);
    fgRealPending_ = !reset;
    return true;
}

void EnhanceChain::recordShowReal(VkCommandBuffer cb) {
    if (!fgActive_ || !fgRealPending_) return;
    copyToDisplay(cb, out8_);
    fgRealPending_ = false;
    if (std::getenv("VEYAMUX_DEBUG_FG")) { static int n = 0; if (++n % 30 == 0) std::fprintf(stderr, "FG: %d real frames shown after generated ones\n", n); }
}

void EnhanceChain::recordStream(VkCommandBuffer cb, const ImageRes& src, int delayMs) {
    if (!src.image || src.width == 0) return;
    int mh = streamMaxH_.load();
    int tw = int(src.width), th = int(src.height);
    if (mh > 0 && th > mh) { tw = int(std::lround(double(tw) * mh / th)); th = mh; }
    tw = std::max(2, tw & ~1); th = std::max(2, th & ~1);

    std::lock_guard l(streamM_);
    StreamSlot* slot = nullptr;
    for (auto& sl : stream_) if (!sl.pending) { slot = &sl; break; }
    if (!slot) return;   // the reader is behind: this frame is not streamed (the screen is unaffected)
    if (slot->img.width != uint32_t(tw) || slot->img.height != uint32_t(th)) {
        ctx_.destroy(slot->img);
        ctx_.destroy(slot->buf);
        slot->img = ctx_.createImage(uint32_t(tw), uint32_t(th), VK_FORMAT_R8G8B8A8_UNORM,
                                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        slot->buf = ctx_.createBuffer(VkDeviceSize(tw) * th * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        if (!slot->img.image || !slot->buf.buffer) { ctx_.destroy(slot->img); ctx_.destroy(slot->buf); return; }
    }
    if (!slot->ev) {
        VkEventCreateInfo ei{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
        if (vkCreateEvent(ctx_.device(), &ei, nullptr, &slot->ev) != VK_SUCCESS) return;
    }
    struct { uint32_t sw, sh, dw, dh, pq, p0, p1, p2; } dp{src.width, src.height, uint32_t(tw), uint32_t(th), outPq_ ? 1u : 0u, 0, 0, 0};
    run("LxDownscale", {&src}, {&slot->img}, &dp, sizeof dp, tw, th, 16);
    ctx_.computeBarrier();
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {uint32_t(tw), uint32_t(th), 1};
    vkCmdCopyImageToBuffer(cb, slot->img.image, VK_IMAGE_LAYOUT_GENERAL, slot->buf.buffer, 1, &c);
    VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
    vkCmdSetEvent(cb, slot->ev, VK_PIPELINE_STAGE_TRANSFER_BIT);
    slot->pending = true;
    slot->seq = ++streamSeq_;
    slot->delayMs = delayMs;
}

void EnhanceChain::pollStream(std::vector<StreamFrame>& out) {
    std::lock_guard l(streamM_);
    for (;;) {
        StreamSlot* oldest = nullptr;
        for (auto& sl : stream_)
            if (sl.pending && (!oldest || sl.seq < oldest->seq)) oldest = &sl;
        if (!oldest || vkGetEventStatus(ctx_.device(), oldest->ev) != VK_EVENT_SET) return;
        VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        r.memory = oldest->buf.memory;
        r.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(ctx_.device(), 1, &r);
        StreamFrame f;
        f.w = int(oldest->img.width); f.h = int(oldest->img.height); f.delayMs = oldest->delayMs;
        const auto* p = static_cast<const uint8_t*>(oldest->buf.mapped);
        f.rgba.assign(p, p + size_t(f.w) * f.h * 4);
        vkResetEvent(ctx_.device(), oldest->ev);
        oldest->pending = false;
        out.push_back(std::move(f));
    }
}


// ------------------------------------------------------------------ recording / screenshot readback

void EnhanceChain::recordRec(VkCommandBuffer cb, const ImageRes& src, std::chrono::steady_clock::time_point t) {
    if (!src.image || src.width == 0) return;
    const int mh = recMaxH_.load();
    int tw = int(src.width), th = int(src.height);
    if (mh > 0 && th > mh) { tw = int(std::lround(double(tw) * mh / th)); th = mh; }
    tw = std::max(2, tw & ~1); th = std::max(2, th & ~1);
    std::lock_guard l(streamM_);
    RecSlot* slot = nullptr;
    for (auto& sl : rec_) if (!sl.pending) { slot = &sl; break; }
    if (!slot) return;   // the encoder is behind: this frame is dropped from the recording only
    if (slot->y.width != uint32_t(tw) || slot->y.height != uint32_t(th)) {
        ctx_.destroy(slot->y); ctx_.destroy(slot->uv); ctx_.destroy(slot->buf);
        const VkImageUsageFlags u = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        slot->y = ctx_.createImage(uint32_t(tw), uint32_t(th), VK_FORMAT_R8_UNORM, u);
        slot->uv = ctx_.createImage(uint32_t(tw / 2), uint32_t(th / 2), VK_FORMAT_R8G8_UNORM, u);
        slot->buf = ctx_.createBuffer(VkDeviceSize(tw) * th * 3 / 2, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        if (!slot->y.image || !slot->uv.image || !slot->buf.buffer) {
            ctx_.destroy(slot->y); ctx_.destroy(slot->uv); ctx_.destroy(slot->buf); return;
        }
    }
    if (!slot->ev) {
        VkEventCreateInfo ei{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
        if (vkCreateEvent(ctx_.device(), &ei, nullptr, &slot->ev) != VK_SUCCESS) return;
    }
    struct { uint32_t sw, sh, dw, dh, pq, p0, p1, p2; } np{src.width, src.height, uint32_t(tw), uint32_t(th), outPq_ ? 1u : 0u, 0, 0, 0};
    run("LxNv12", {&src}, {&slot->y, &slot->uv}, &np, sizeof np, tw / 2, th / 2, 8);
    VkBufferImageCopy c[2]{};
    c[0].imageSubresource = c[1].imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c[0].imageExtent = {uint32_t(tw), uint32_t(th), 1};
    vkCmdCopyImageToBuffer(cb, slot->y.image, VK_IMAGE_LAYOUT_GENERAL, slot->buf.buffer, 1, &c[0]);
    c[1].bufferOffset = VkDeviceSize(tw) * th;
    c[1].imageExtent = {uint32_t(tw / 2), uint32_t(th / 2), 1};
    vkCmdCopyImageToBuffer(cb, slot->uv.image, VK_IMAGE_LAYOUT_GENERAL, slot->buf.buffer, 1, &c[1]);
    VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
    vkCmdSetEvent(cb, slot->ev, VK_PIPELINE_STAGE_TRANSFER_BIT);
    slot->pending = true;
    slot->seq = ++recSeq_;
    slot->t = t.time_since_epoch().count() ? t : std::chrono::steady_clock::now();
}

void EnhanceChain::pollRecord(std::vector<RecFrame>& out) {
    std::lock_guard l(streamM_);
    for (;;) {
        RecSlot* oldest = nullptr;
        for (auto& sl : rec_)
            if (sl.pending && (!oldest || sl.seq < oldest->seq)) oldest = &sl;
        if (!oldest || vkGetEventStatus(ctx_.device(), oldest->ev) != VK_EVENT_SET) return;
        VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        r.memory = oldest->buf.memory;
        r.size = VK_WHOLE_SIZE;
        vkInvalidateMappedMemoryRanges(ctx_.device(), 1, &r);
        RecFrame f;
        f.w = int(oldest->y.width); f.h = int(oldest->y.height); f.t = oldest->t;
        const auto* p = static_cast<const uint8_t*>(oldest->buf.mapped);
        f.nv12.assign(p, p + size_t(f.w) * f.h * 3 / 2);
        vkResetEvent(ctx_.device(), oldest->ev);
        oldest->pending = false;
        out.push_back(std::move(f));
    }
}

void EnhanceChain::recordShot(VkCommandBuffer cb, const ImageRes& src) {
    if (!src.image || src.width == 0) return;
    std::lock_guard l(streamM_);
    if (shot_.pending) return;
    if (shot_.buf.size < VkDeviceSize(src.width) * src.height * 4 || shot_.img.width != src.width || shot_.img.height != src.height) {
        ctx_.destroy(shot_.buf);
        shot_.buf = ctx_.createBuffer(VkDeviceSize(src.width) * src.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true);
        if (!shot_.buf.buffer) return;
        shot_.img.width = src.width; shot_.img.height = src.height;   // size bookkeeping only (no image owned)
    }
    if (!shot_.ev) {
        VkEventCreateInfo ei{VK_STRUCTURE_TYPE_EVENT_CREATE_INFO};
        if (vkCreateEvent(ctx_.device(), &ei, nullptr, &shot_.ev) != VK_SUCCESS) return;
    }
    VkBufferImageCopy c{};
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    c.imageExtent = {src.width, src.height, 1};
    vkCmdCopyImageToBuffer(cb, src.image, VK_IMAGE_LAYOUT_GENERAL, shot_.buf.buffer, 1, &c);
    VkMemoryBarrier hb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    hb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, nullptr, 0, nullptr);
    vkCmdSetEvent(cb, shot_.ev, VK_PIPELINE_STAGE_TRANSFER_BIT);
    shot_.pending = true;
}

bool EnhanceChain::pollScreenshot(StreamFrame& out) {
    std::lock_guard l(streamM_);
    if (!shot_.pending || vkGetEventStatus(ctx_.device(), shot_.ev) != VK_EVENT_SET) return false;
    VkMappedMemoryRange r{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
    r.memory = shot_.buf.memory;
    r.size = VK_WHOLE_SIZE;
    vkInvalidateMappedMemoryRanges(ctx_.device(), 1, &r);
    out.w = int(shot_.img.width); out.h = int(shot_.img.height);
    const auto* p = static_cast<const uint8_t*>(shot_.buf.mapped);
    out.rgba.assign(p, p + size_t(out.w) * out.h * 4);
    vkResetEvent(ctx_.device(), shot_.ev);
    shot_.pending = false;
    return true;
}

void EnhanceChain::releaseStream() {
    std::lock_guard l(streamM_);
    bool any = shot_.buf.buffer || shot_.ev;
    for (auto& sl : stream_) any = any || sl.img.image || sl.ev;
    for (auto& sl : rec_) any = any || sl.y.image || sl.ev;
    if (!any) return;
    vkDeviceWaitIdle(ctx_.device());
    for (auto& sl : rec_) {
        ctx_.destroy(sl.y); ctx_.destroy(sl.uv); ctx_.destroy(sl.buf);
        if (sl.ev) vkDestroyEvent(ctx_.device(), sl.ev, nullptr);
        sl = RecSlot{};
    }
    ctx_.destroy(shot_.buf);
    if (shot_.ev) vkDestroyEvent(ctx_.device(), shot_.ev, nullptr);
    shot_ = StreamSlot{};
    for (auto& sl : stream_) {
        ctx_.destroy(sl.img);
        ctx_.destroy(sl.buf);
        if (sl.ev) vkDestroyEvent(ctx_.device(), sl.ev, nullptr);
        sl = StreamSlot{};
    }
}

} // namespace veyamux
