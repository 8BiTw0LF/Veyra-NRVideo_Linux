#include "MotionEstimator.h"

#include <algorithm>
#include <fstream>
#include <vector>

namespace veyamux {

MotionEstimator::MotionEstimator(VkContext& ctx, std::string shaderDir) : ctx_(ctx), dir_(std::move(shaderDir)) {}

MotionEstimator::~MotionEstimator() {
    release();
    for (VkPipeline p : {luma_, lk_, fill_, select_}) if (p) ctx_.destroy(p);
}

void MotionEstimator::release() {
    if ((levels_ > 0) && ctx_.device()) vkDeviceWaitIdle(ctx_.device());
    for (int i = 0; i < kMaxLevels; ++i) { ctx_.destroy(lumA_[i]); ctx_.destroy(lumB_[i]); ctx_.destroy(field_[i]); ctx_.destroy(fieldT_[i]); }
    levels_ = 0;
    w_ = h_ = 0;
    havePrev_ = false;
}

bool MotionEstimator::ensure(uint32_t w, uint32_t h, std::string& error) {
    if (levels_ > 0 && w == w_ && h == h_) return true;
    auto load = [&](const char* name, VkPipeline& out) {
        if (out) return true;
        std::ifstream f(dir_ + "/" + name + ".spv", std::ios::binary | std::ios::ate);
        if (!f) { error = std::string("Missing shader ") + name + ".spv"; return false; }
        const size_t n = size_t(f.tellg());
        f.seekg(0);
        std::vector<uint32_t> code((n + 3) / 4);
        f.read(reinterpret_cast<char*>(code.data()), std::streamsize(n));
        out = ctx_.createComputePipeline(code, error);
        return out != VK_NULL_HANDLE;
    };
    if (!load("LxLuma", luma_) || !load("LxLkLevel", lk_) || !load("LxMvFill", fill_) || !load("LxMvSelect", select_)) return false;
    release();
    const VkImageUsageFlags u = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    // Enough levels that the coarsest is still ~16+ pixels high.
    int levels = 1;
    while (levels < kMaxLevels && std::min(w, h) >> levels >= 16) ++levels;
    uint32_t lw = w, lh = h;
    bool ok = true;
    for (int i = 0; i < levels; ++i) {
        lumA_[i] = ctx_.createImage(lw, lh, ctx_.lumaFormat(), u);
        lumB_[i] = ctx_.createImage(lw, lh, ctx_.lumaFormat(), u);
        field_[i] = ctx_.createImage(lw, lh, VK_FORMAT_R16G16B16A16_SFLOAT, u);
        fieldT_[i] = ctx_.createImage(lw, lh, VK_FORMAT_R16G16B16A16_SFLOAT, u);
        ok = ok && lumA_[i].image && lumB_[i].image && field_[i].image && fieldT_[i].image;
        lw = (lw + 1) / 2; lh = (lh + 1) / 2;
    }
    levels_ = levels;
    if (!ok) { release(); error = "Out of GPU memory for motion estimation"; return false; }
    w_ = w; h_ = h;
    havePrev_ = false;
    return true;
}

const ImageRes* MotionEstimator::record(VkCommandBuffer cb, const ImageRes& input, bool reset, std::string& error) {
    if (!ensure(input.width, input.height, error)) return nullptr;
    if (reset) havePrev_ = false;

    // 1) luma pyramid of this frame (the previous frame's pyramid is kept in the other set)
    curIsA_ = !curIsA_;
    ImageRes* curP = curIsA_ ? lumA_ : lumB_;
    ImageRes* prevP = curIsA_ ? lumB_ : lumA_;
    struct { uint32_t dw, dh, sw, sh, mode, p0, p1, p2; } lp{w_, h_, w_, h_, 0, 0, 0, 0};
    {
        const ImageRes* in[] = {&input};
        const ImageRes* out[] = {&curP[0]};
        ctx_.dispatch(luma_, in, 1, out, 1, &lp, sizeof lp, (w_ + 15) / 16, (h_ + 15) / 16);
        ctx_.computeBarrier();
    }
    for (int i = 1; i < levels_; ++i) {
        lp = {curP[i].width, curP[i].height, curP[i - 1].width, curP[i - 1].height, 1, 0, 0, 0};
        const ImageRes* in[] = {nullptr, &curP[i - 1]};
        const ImageRes* out[] = {&curP[i]};
        ctx_.dispatch(luma_, in, 2, out, 1, &lp, sizeof lp, (curP[i].width + 15) / 16, (curP[i].height + 15) / 16);
        ctx_.computeBarrier();
    }

    if (!havePrev_) {
        VkClearColorValue zero{};
        VkImageSubresourceRange rng{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cb, field_[0].image, VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &rng);
        ctx_.computeBarrier();
        havePrev_ = true;
        return &field_[0];
    }

    // 2) coarse-to-fine Lucas-Kanade against the previous pyramid, filling unreliable vectors on every level
    // Frames wider than 2560 (4K) are estimated down to half resolution and the field is upsampled for level 0: the
    // full-resolution pass would cost ~4x more GPU time for detail frame generation does not need.
    const bool halfRes = w_ > 2560 && levels_ > 1;
    for (int i = levels_ - 1; i >= 0; --i) {
        const bool top = i == levels_ - 1;
        const bool upsampleOnly = halfRes && i == 0;
        struct { uint32_t w, h, cw, ch, hasCoarse, iterations, p0, p1; } kp{
            field_[i].width, field_[i].height, top ? 1u : field_[i + 1].width, top ? 1u : field_[i + 1].height,
            top ? 0u : 1u, upsampleOnly ? 0u : 3u, 0, 0};
        const ImageRes* in[] = {&curP[i], &prevP[i], top ? nullptr : &field_[i + 1]};
        if (upsampleOnly) {
            const ImageRes* out[] = {&field_[0]};
            ctx_.dispatch(lk_, in, 3, out, 1, &kp, sizeof kp, (kp.w + 7) / 8, (kp.h + 7) / 8);
            ctx_.computeBarrier();
            break;
        }
        const ImageRes* out[] = {&fieldT_[i]};
        ctx_.dispatch(lk_, in, 3, out, 1, &kp, sizeof kp, (kp.w + 7) / 8, (kp.h + 7) / 8);
        ctx_.computeBarrier();
        // ping-pong fieldT <-> field, ending in field. The top level has no coarser estimate to fall back on, so it
        // gets enough passes to spread across the frame.
        const int passes = top ? 9 : (i == levels_ - 2 ? 3 : 1);   // odd: ends in field_
        for (int k = 0; k < passes; ++k) {
            struct { uint32_t w, h; float keep; uint32_t p0; } fp{kp.w, kp.h, 0.3f, 0};
            const bool even = (k % 2) == 0;
            const ImageRes* fin[] = {even ? &fieldT_[i] : &field_[i]};
            const ImageRes* fout[] = {even ? &field_[i] : &fieldT_[i]};
            ctx_.dispatch(fill_, fin, 1, fout, 1, &fp, sizeof fp, (kp.w + 7) / 8, (kp.h + 7) / 8);
            ctx_.computeBarrier();
        }
        // sharpen motion boundaries on the finest estimated level (select into fieldT, copy back into field)
        if (i == (halfRes ? 1 : 0)) {
            struct { uint32_t w, h, p0, p1; } sp{kp.w, kp.h, 0, 0};
            const ImageRes* sin[] = {&curP[i], &prevP[i], &field_[i]};
            const ImageRes* sout[] = {&fieldT_[i]};
            ctx_.dispatch(select_, sin, 3, sout, 1, &sp, sizeof sp, (kp.w + 7) / 8, (kp.h + 7) / 8);
            ctx_.computeBarrier();
            VkImageCopy c{};
            c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            c.extent = {kp.w, kp.h, 1};
            vkCmdCopyImage(cb, fieldT_[i].image, VK_IMAGE_LAYOUT_GENERAL, field_[i].image, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
            ctx_.computeBarrier();
        }
    }
    return &field_[0];
}

} // namespace veyamux
