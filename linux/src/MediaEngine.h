// VeyAmux for Linux - media engine (Qt-free).
//   demux thread -> video decode thread -> render thread (Vulkan enhancement chain) -> onFrame()
//                -> audio thread (decode + PulseAudio/PipeWire output; audio is the master clock)
#pragma once
#include <algorithm>
#include <array>
#include "CaptureSource.h"
#include "EnhanceChain.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AVFormatContext;
struct AVCodecContext;
struct AVPacket;
struct AVFrame;
struct SwsContext;
struct SwrContext;
struct AVBufferRef;

namespace veyamux {

struct MediaInfo {
    std::string title;
    double duration = 0;
    int width = 0, height = 0;
    double fps = 0;
    std::string videoCodec, audioCodec;
    bool hasVideo = false, hasAudio = false;
    bool hwDecode = false;
};

struct EngineStats {
    double processMs = 0;      // GPU chain time (upload + passes + readback) of the last frame
    uint64_t rendered = 0;
    uint64_t dropped = 0;
    int outW = 0, outH = 0;
    bool gpuActive = false;
    std::string upError;       // last AMD FSR upscaling error (empty = fine; SGSR is used meanwhile)
    float gpuMs = -1;          // measured GPU time (Vulkan timestamps), smoothed; -1 = unknown
    std::array<float, GpuTimer::kStages> gpuStages{};
};

class MediaEngine {
public:
    struct Callbacks {
        // RGBA8, tightly packed, valid only during the call.
        std::function<void(const uint8_t* rgba, int w, int h, double displayAspect)> onFrame;
        std::function<void()> onEnded;
        std::function<void(const std::string&)> onError;
        std::function<void(const std::string&)> onGpuMessage;
        // Zero-copy live path: raw captured frame (YUYV/UYVY/NV12/RGBA), delivered on the capture thread. The receiver
        // swaps it with a frame it no longer needs, so the capture thread gets an already allocated buffer back (no new
        // allocation and page faults for every picture: 12 MB per 4K NV12 frame).
        std::function<void(RawFrame&)> onLiveFrame;
        // Audio as heard (48 kHz stereo s16, before volume), with the time its first sample plays. Recording tap.
        std::function<void(const int16_t*, int frames, std::chrono::steady_clock::time_point)> onAudio;
    };

    explicit MediaEngine(std::string shaderDir);
    ~MediaEngine();

    void setCallbacks(Callbacks cb) { cb_ = std::move(cb); }
    bool open(const std::string& path, std::string& error);
    void close();
    bool isOpen() const { return fmt_ != nullptr || live_.load(); }
    bool isLive() const { return live_.load(); }
    // Live V4L2 capture (low latency, no A/V clock). audioSource = PulseAudio/PipeWire source name, empty = no audio.
    bool openCapture(const std::string& devicePath, const CaptureMode& mode, const std::string& audioSource,
                     std::string& error, bool rawToGpu = false);
    bool isRawLive() const { return rawLive_.load(); }
    // Live capture only: what the signal actually carries (content frame rate, estimated rendering resolution).
    ContentProbe::Result liveContent() const { return live_.load() && cap_ ? cap_->content() : ContentProbe::Result{}; }
    const MediaInfo& info() const { return info_; }

    void play();
    void pause();
    bool playing() const { return playing_.load(); }
    void seek(double seconds);
    double position() const;
    void setVolume(float v) { volume_ = v; }
    void setMuted(bool m) { muted_ = m; }
    // Output device (PulseAudio/PipeWire sink name, empty = system default); switched while playing.
    void setAudioSink(const std::string& sink) { { std::lock_guard l(sinkM_); sink_ = sink; } ++sinkGen_; }
    // Even out loud and quiet passages (slow automatic gain with a peak limiter), applied to what you hear only.
    void setNormalize(bool on) { normalize_ = on; }
    // Audio offset in ms, positive = sound later than the picture. Files: -300..+300 (the picture is moved against the
    // audio clock). Capture: 0..+300 (sound is held back; live audio cannot be played earlier than it arrives).
    void setAudioDelay(int ms) { audioDelayMs_ = std::clamp(ms, -300, 300); }
    void setEnhance(const EnhanceSettings& s);
    EngineStats stats() const;
    bool hwDecodeActive() const { return hwActive_.load(); }
    std::string gpuName() const;

private:
    struct Pkt { AVPacket* pkt = nullptr; int serial = 0; enum Kind { Data, Flush, Eof } kind = Data; };
    struct PacketQueue {
        std::mutex m; std::condition_variable cv;
        std::deque<Pkt> q; size_t bytes = 0; size_t dataCount = 0; bool abort = false;
    };
    // Picture buffers are reused: a frame hands its buffer back to the pool when it is dropped, so playback does not
    // allocate (and page-fault) 8-33 MB for every picture.
    struct BufferPool {
        std::mutex m;
        std::vector<std::vector<uint8_t>> free;
        std::vector<uint8_t> take(size_t bytes) {
            std::vector<uint8_t> v;
            {
                std::lock_guard l(m);
                for (size_t i = 0; i < free.size(); ++i)
                    if (free[i].capacity() >= bytes) { v.swap(free[i]); free.erase(free.begin() + long(i)); break; }
            }
            v.resize(bytes);
            return v;
        }
        void give(std::vector<uint8_t>&& v) {
            if (v.capacity() == 0) return;
            std::lock_guard l(m);
            if (free.size() < 6) free.push_back(std::move(v));
        }
    };
    struct Frame {
        std::vector<uint8_t> rgba; int w = 0, h = 0; double pts = 0, aspect = 1; int serial = 0; bool eos = false;
        int hdr = 0;   // 0 SDR (rgba = RGBA8), 1 PQ, 2 HLG (rgba = RGBA64: 16-bit R'G'B' in BT.2020)
        std::shared_ptr<BufferPool> pool;   // where rgba goes back to (empty: freed normally)
        Frame() = default;
        Frame(const Frame&) = delete;
        Frame& operator=(const Frame&) = delete;
        ~Frame() { if (pool) pool->give(std::move(rgba)); }
    };
    std::shared_ptr<BufferPool> pool_ = std::make_shared<BufferPool>();
    struct Clock {
        std::mutex m; double pts = 0; std::chrono::steady_clock::time_point wall; bool running = false;
        void set(double p);
        double get();
        void setRunning(bool r);
    };

    void demuxLoop();
    void videoLoop();
    void audioLoop();
    void renderLoop();
    void captureLoop();
    void captureAudioLoop();
    bool openVideoDecoder(bool tryHw, std::string& error);
    void pushPacket(PacketQueue& q, Pkt p);
    bool popPacket(PacketQueue& q, Pkt& out);
    void clearQueue(PacketQueue& q);
    void ensureGpu();       // runs on gpuThread_
    void startGpu();
    void joinAll();

    Callbacks cb_;
    std::string shaderDir_;
    MediaInfo info_;

    AVFormatContext* fmt_ = nullptr;
    AVCodecContext* vctx_ = nullptr;
    AVCodecContext* actx_ = nullptr;
    AVBufferRef* hwDevice_ = nullptr;
    int vIndex_ = -1, aIndex_ = -1;
    double vTimeBase_ = 0, aTimeBase_ = 0, startTime_ = 0;

    PacketQueue vpkts_, apkts_;
    std::mutex fq_m_; std::condition_variable fq_cv_; std::deque<std::shared_ptr<Frame>> frames_;
    Clock clock_;

    std::thread demux_, vdec_, adec_, render_, capThread_, capAudioThread_;
    std::unique_ptr<CaptureSource> cap_;
    std::atomic<bool> live_{false};
    std::atomic<bool> rawLive_{false};
    std::atomic<bool> skipRepeats_{false};   // live: drop repeated pictures (frame generation or VRR pacing is on)
    std::atomic<bool> vrrPacing_{false};     // live: space the game's pictures evenly (VRR display)
    std::string capAudioSource_;
    std::string capPath_;          // for reconnecting after the card drops out or stalls on a signal change
    CaptureMode capMode_{};
    std::chrono::steady_clock::time_point liveStart_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> playing_{false};
    std::atomic<int> serial_{1};
    std::atomic<double> seekReq_{-1.0};
    std::atomic<double> seekTarget_{0.0};   // decoders drop frames before this after a seek
    std::atomic<bool> showOne_{false};      // display exactly one frame while paused (after seek)
    std::atomic<bool> eof_{false};
    std::atomic<bool> redraw_{false};      // re-present the last frame (settings changed while paused)
    std::atomic<bool> hwActive_{false};
    std::atomic<bool> endedFired_{false};
    std::atomic<float> volume_{1.0f};
    std::atomic<bool> muted_{false};
    std::mutex sinkM_;
    std::string sink_;
    std::atomic<int> sinkGen_{0};
    std::atomic<bool> normalize_{false};
    std::string sinkName() { std::lock_guard l(sinkM_); return sink_; }
    std::atomic<int> audioDelayMs_{0};
    std::mutex gate_m_; std::condition_variable gate_cv_;   // wakes threads on pause/resume/seek/quit

    std::mutex set_m_; EnhanceSettings settings_;
    mutable std::mutex stat_m_; EngineStats stats_;

    std::unique_ptr<VkContext> vk_;
    std::unique_ptr<EnhanceChain> chain_;
    std::shared_ptr<Frame> lastFrame_;     // render thread only
    std::thread gpuThread_;
    std::atomic<bool> gpuReady_{false};
    std::atomic<bool> gpuStarted_{false};
    std::string gpuName_;
};

} // namespace veyamux
