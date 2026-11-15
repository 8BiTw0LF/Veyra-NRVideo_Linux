// VeyAmux for Linux - dense sub-pixel motion between consecutive frames of a video (no engine motion vectors exist).
//
// Coarse-to-fine Lucas-Kanade on a luma pyramid (shaders LxLuma, LxLkLevel, LxMvFill):
//  * sub-pixel accurate, valid from the second frame after a reset (FidelityFX optical flow is whole-pixel per 8x8
//    block and reports no motion for the first frames after a reset or a detected scene change - frame generation then
//    blends two frames, which shows as double edges on pans),
//  * up to 7 levels: each level corrects up to 2 of its pixels, so 4K follows ~250 px/frame, 1080p ~120 px/frame,
//  * unreliable vectors (flat areas, lone edges, poor matches) are filled from confident neighbours on every level,
//  * motion boundaries are sharpened at the end (LxMvSelect: best-matching neighbour vector per pixel).
// Output: RGBA16F (mv.x, mv.y, confidence, 0) in pixels, convention "this pixel was at p + mv in the previous frame"
// (what FSR upscaling and FSR frame interpolation expect).
#pragma once
#include "VkContext.h"
#include <string>

namespace veyamux {

class MotionEstimator {
public:
    MotionEstimator(VkContext& ctx, std::string shaderDir);
    ~MotionEstimator();
    MotionEstimator(const MotionEstimator&) = delete;
    MotionEstimator& operator=(const MotionEstimator&) = delete;

    // Records into the context's active command buffer. `input`: any sampled colour image of size w x h (GENERAL).
    // reset: no usable previous frame (the field is cleared). Returns the motion field (valid until the next call).
    const ImageRes* record(VkCommandBuffer cb, const ImageRes& input, bool reset, std::string& error);
    const ImageRes& field() const { return field_[0]; }
    void release();

    static constexpr int kMaxLevels = 7;

private:
    bool ensure(uint32_t w, uint32_t h, std::string& error);
    VkContext& ctx_;
    std::string dir_;
    VkPipeline luma_ = VK_NULL_HANDLE, lk_ = VK_NULL_HANDLE, fill_ = VK_NULL_HANDLE, select_ = VK_NULL_HANDLE;
    ImageRes lumA_[kMaxLevels], lumB_[kMaxLevels], field_[kMaxLevels], fieldT_[kMaxLevels];
    int levels_ = 0;
    bool curIsA_ = true, havePrev_ = false;
    uint32_t w_ = 0, h_ = 0;
};

} // namespace veyamux
