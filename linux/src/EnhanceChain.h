// VeyAmux for Linux - GPU enhancement chain built from the original Veyra (Windows) HLSL shaders (compiled to SPIR-V).
//   RGBA8 -> RgbToLinear -> [AaLuma+AaFxaa] -> [Lanczos or AMD FSR 3.1 upscale] -> [Sharpen/Grade] -> ScaleBlit(sRGB encode) -> RGBA8
#pragma once
#include <chrono>
#include <atomic>
#include <mutex>
#include "FsrUpscaler.h"
#include "MotionEstimator.h"
#include "GpuTimer.h"
#include <array>
#include "VkContext.h"
#include <map>
#include <string>
#include <vector>

namespace veyamux {

// Frame generation modes. Both are applied by the separately installed lsfg-vk Vulkan layer (Lossless Scaling) on
// VeyAmux's window after VeyAmux has shown its frame, so the chain itself does nothing for them. Values 1 and 4 were
// in-app frame generation (AMD FSR 3.1, later RIFE) and are read as LsfgFixed.
enum class FrameGenMode { Off = 0, LsfgFixed = 2, LsfgFixed3 = 3 };   // Lossless Scaling 2x / 3x
// Denoisers (run first, on the source-size linear image, before FXAA/upscaling/sharpening).
enum class Denoise { Off = 0, Spatial = 1, Temporal = 2, Both = 3 };
// Upscaling method used when scale > 1. AmdFsr3 falls back to Lanczos if FidelityFX cannot run.
enum class Upscaler { Lanczos = 0, AmdFsr3 = 1, AmdFsr1 = 2 };   // FSR 1 = EASU + RCAS (spatial, one frame)

struct EnhanceSettings {
    bool enabled = true;
    float scale = 1.0f;       // output scale factor (>=1)
    Upscaler upscaler = Upscaler::Lanczos;
    bool fxaa = false;
    Denoise denoise = Denoise::Off;
    int denoiseStrength = 1;  // 0 light, 1 medium, 2 strong
    float hdrPeak = 1000.0f;  // HDR capture (P010): brightest level of the source in nits, tone-mapped to SDR white
    bool hdrOut = false;      // HDR display output (scRGB swapchain): out8_ holds PQ-coded light instead of sRGB
    float displayPeak = 1000.0f;   // HDR output: brightest level of the display in nits (HDR sources are mapped to it)
    bool vrr = false;         // live: show the game's own pictures at an even pace (for VRR displays; frame generation off)
    int fxaaQuality = 1;      // 0 medium, 1 high, 2 ultra (VeyAmux AaFxaa3/4/5)
    float sharpen = 0.0f;     // 0..1
    float exposure = 0.0f;    // EV
    float contrast = 1.0f;
    float saturation = 1.0f;
    FrameGenMode frameGen = FrameGenMode::Off;
    // Frame generation is independent of the "Enhance" switch (that one only covers upscale/AA/sharpen/grade).
    bool lsfgFg() const { return frameGen == FrameGenMode::LsfgFixed || frameGen == FrameGenMode::LsfgFixed3; }

    bool isIdentity() const {
        return (!enabled || (scale <= 1.001f && !fxaa && denoise == Denoise::Off && sharpen <= 0.001f && exposure == 0.0f &&
                            contrast == 1.0f && saturation == 1.0f));
    }
};

// A raw frame handed to the zero-copy live path (everything after the upload happens on the GPU).
struct LiveInput {
    enum Format { RGBA, YUYV, UYVY, NV12, P010 } fmt = RGBA;   // P010: HDR (PQ, BT.2020), tone-mapped to SDR
    std::chrono::steady_clock::time_point time{};               // capture time (recording timestamps)
    const uint8_t* data = nullptr;
    size_t size = 0;
    int w = 0, h = 0, stride = 0;     // stride in bytes (Y-plane stride for NV12)
    bool fullRange = false;
};

class EnhanceChain {
public:
    EnhanceChain(VkContext& ctx, std::string shaderDir);
    ~EnhanceChain();

    bool init(std::string& error);
    // Runs the chain on a tightly- or loosely-packed RGBA8 frame. Output is tightly packed RGBA8.
    // frameMs: time since the previous source frame (for the temporal passes).
    bool process(const uint8_t* rgba, int w, int h, int strideBytes, const EnhanceSettings& s,
                 std::vector<uint8_t>& out, int& outW, int& outH, std::string& error, float frameMs = 1000.0f / 30.0f,
                 int hdr = 0);   // hdr: 0 RGBA8; 1 PQ / 2 HLG = RGBA64 (16-bit R'G'B', BT.2020), tone-mapped to SDR
    // Zero-copy live path: records upload + conversion + enhancement into a command buffer that belongs to the
    // caller (Qt's frame). No submit, no fence, no readback: the result is liveOutput(), an RGBA8 GENERAL image.
    bool recordLive(VkCommandBuffer cb, int slot, const LiveInput& in, const EnhanceSettings& s, std::string& error,
                    float frameMs = 1000.0f / 60.0f);
    const ImageRes& liveOutput() const { return out8_; }
    // Measured GPU time (Vulkan timestamps), smoothed: total and per stage; -1 when not available.
    float gpuMs() const { return timer_.totalMs(); }
    std::array<float, GpuTimer::kStages> gpuStageMs() const { return timer_.stageMs(); }
    std::string upError() const { return upError_; }
    FsrUpscaler* upscaler() { return fsrUp_.get(); }   // test hook
    uint64_t liveGeneration() const { return liveGen_; }
    void resetHistory() { upHistory_ = false; nrHistory_ = false; }
    static void outputSize(int w, int h, const EnhanceSettings& s, int& ow, int& oh);

    // OBS stream (live path): when maxHeight >= 0, every displayed image is downscaled to fit
    // maxHeight (0 = original size) and copied to host memory inside the live command buffer. pollStream() (any
    // thread) returns the copies the GPU has finished, oldest first.
    struct StreamFrame { std::vector<uint8_t> rgba; int w = 0, h = 0; int delayMs = 0; };
    void setStreamMaxHeight(int h) { streamMaxH_ = h; }
    void pollStream(std::vector<StreamFrame>& out);

    // Recording (live path): when maxHeight >= 0 every displayed image is converted to NV12 at that height
    // (0 = original size) and copied to host memory, like the OBS stream. pollRecord() returns the finished copies.
    struct RecFrame { std::vector<uint8_t> nv12; int w = 0, h = 0; std::chrono::steady_clock::time_point t{}; };
    void setRecordMaxHeight(int h) { recMaxH_ = h; }
    void pollRecord(std::vector<RecFrame>& out);
    // Screenshot (live path): the next displayed image is copied at full size; pollScreenshot() returns it once.
    void requestScreenshot() { shotWanted_ = true; }
    bool pollScreenshot(StreamFrame& out);

private:
    struct StreamSlot {
        ImageRes img; BufferRes buf; VkEvent ev = VK_NULL_HANDLE;
        bool pending = false; uint64_t seq = 0; int delayMs = 0;
    };
    static constexpr int kStreamSlots = 6;
    StreamSlot stream_[kStreamSlots];
    std::mutex streamM_;
    std::atomic<int> streamMaxH_{-1};
    uint64_t streamSeq_ = 0;
    void recordStream(VkCommandBuffer cb, const ImageRes& src, int delayMs);
    void releaseStream();
    struct RecSlot {
        ImageRes y, uv; BufferRes buf; VkEvent ev = VK_NULL_HANDLE;
        bool pending = false; uint64_t seq = 0; std::chrono::steady_clock::time_point t{};
    };
    static constexpr int kRecSlots = 6;
    RecSlot rec_[kRecSlots];
    std::atomic<int> recMaxH_{-1};
    uint64_t recSeq_ = 0;
    void recordRec(VkCommandBuffer cb, const ImageRes& src, std::chrono::steady_clock::time_point t);
    StreamSlot shot_;
    std::atomic<bool> shotWanted_{false};
    void recordShot(VkCommandBuffer cb, const ImageRes& src);
    void run(const char* k, std::initializer_list<const ImageRes*> in, std::initializer_list<const ImageRes*> outs,
             const void* ubo, uint32_t uboSize, int dw, int dh, int g);
    void recordPost(const ImageRes* start, const EnhanceSettings& s);
    // HDR sources: the level the source is tone-mapped to (1.0 in the working image): SDR white, or the display's peak
    static float hdrTarget(const EnhanceSettings& s) {
        return s.hdrOut ? std::clamp(std::min(s.displayPeak, s.hdrPeak), 203.0f, 10000.0f) : 203.0f;
    }
    float frameNits_ = 203.0f;
    std::atomic<bool> outPq_{false};
    uint32_t pqFrame_ = 0;
    void retire(ImageRes& img);
    void collectRetired();
    bool ensureLive(int w, int h, int ow, int oh, std::string& error);
    bool loadKernel(const std::string& name, std::string& error);
    bool ensureResources(int w, int h, int ow, int oh, std::string& error);
    void releaseResources();
    VkPipeline kernel(const char* name) const;

    VkContext& ctx_;
    std::string dir_;
    std::map<std::string, VkPipeline> kernels_;

    int w_ = 0, h_ = 0, ow_ = 0, oh_ = 0;
    ImageRes src_, lin_, linB_, luma_, up_, fin_, out8_;
    std::unique_ptr<FsrUpscaler> fsrUp_;
    // denoisers
    std::unique_ptr<MotionEstimator> nrMotion_;
    GpuTimer timer_;
    ImageRes nrOut_, nrHist_[2];
    int nrCur_ = 0;
    bool nrHistory_ = false;          // the temporal denoiser has a previous result of this sequence
    const ImageRes* recordDenoise(const ImageRes* in, const EnhanceSettings& s);
    bool upHistory_ = false;          // FSR upscaling has seen the previous frame of this sequence
    std::string upError_;
    uint64_t upFailedKey_ = 0;        // sizes at which FSR upscaling failed to start (0 = none)
    float frameMs_ = 1000.0f / 30.0f; // time since the previous source frame (for the temporal passes)
    BufferRes staging_, readback_;
    // live path
    ImageRes rawPacked_, rawY_, rawUV_, rawY16_, rawUV16_, src16_;   // src16_: HDR video (RGBA64)
    BufferRes staging16_;   // the 16-bit planes only exist for P010 (HDR)
    BufferRes liveStaging_[VkContext::kSlots];
    VkDeviceSize liveStagingSize_ = 0;
    int liveW_ = 0, liveH_ = 0;
    uint64_t liveGen_ = 0;
    bool liveMode_ = false;
    struct Retired { ImageRes img; int framesLeft; };
    std::vector<Retired> retired_;   // old output images stay alive until Qt has dropped its texture wrapper
};

} // namespace veyamux
