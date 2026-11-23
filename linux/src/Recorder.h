// VeyAmux for Linux: instant replay and recording.
//
// One encoder session runs while instant replay or a recording is on. Video arrives as NV12 (converted on the GPU
// in the live path, by swscale for files) and is encoded in hardware through VA-API (HEVC, then H.264), or with
// libx264 if no VA-API encoder works. Audio arrives as 48 kHz stereo s16 and is encoded to AAC. Encoded packets go
// to a ring buffer (the last N seconds, for "save replay") and, while recording, straight into an MP4 file.
// All encoding and file writing happens on worker threads; the push functions never block on the encoder.
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AVCodecContext;
struct AVBufferRef;
struct AVFormatContext;
struct AVPacket;
struct AVFrame;
struct SwsContext;

namespace veyamux {

class Recorder {
public:
    using Clock = std::chrono::steady_clock;
    struct Status { bool replay = false, recording = false; std::string encoder; double recSeconds = 0; };

    Recorder();
    ~Recorder();

    // Replay buffer on/off and its length (seconds). Turning it off frees the buffer.
    void setReplay(bool on, int seconds);
    bool replayOn() const { return replayOn_.load(); }
    // Recording to a file (MP4). startRecording returns false with a message if the file cannot be opened later on
    // the worker; errors are reported through onMessage.
    void startRecording(const std::string& path);
    void stopRecording();
    bool recording() const { return recording_.load(); }
    // Writes the replay buffer to an MP4 file in the background. Returns false if there is nothing to save yet.
    bool saveReplay(const std::string& path);
    bool active() const { return replayOn_.load() || recording_.load() || streaming_.load(); }

    // Live stream over RTMP(S) (Twitch, YouTube, ...): H.264 at kbps (constant bit rate). While streaming, replay and
    // recordings use the same H.264 encoder. Not while a recording runs with another encoder setting.
    void startStream(const std::string& url, int kbps);
    void stopStream();
    bool streaming() const { return streaming_.load(); }
    double streamSeconds() const;

    // Input (any thread). NV12: Y plane w*h, then interleaved UV w*h/2, tightly packed. Even w/h.
    void pushVideo(std::vector<uint8_t>&& nv12, int w, int h, Clock::time_point t);
    // Converts RGBA (sRGB) to NV12 on the CPU first (file playback path).
    void pushRgba(const uint8_t* rgba, int w, int h, int stride, int maxHeight, Clock::time_point t);
    // 48 kHz interleaved stereo s16; t = when the first sample was heard/captured.
    void pushAudio(const int16_t* samples, int frames, Clock::time_point t);

    std::function<void(const std::string& msg, bool error)> onMessage;   // called from worker threads
    std::string encoderName() const;
    double recordingSeconds() const;

private:
    struct Job {
        std::vector<uint8_t> nv12;    // or RGBA when rgba is set
        int w = 0, h = 0, maxH = 0; bool rgba = false;
        Clock::time_point t; std::vector<int16_t> pcm; bool audio = false;
    };
    struct Params;                   // codec parameters of one encoder session (shared with background savers)
    struct Pkt { AVPacket* p = nullptr; bool video = false; bool key = false; double sec = 0; uint64_t gen = 0; };
    struct Mux;
    struct StreamOut;

    void worker();
    bool openVideo(int w, int h, std::string& err);
    void closeEncoders();
    bool openAudio(std::string& err);
    void encodeVideo(Job& j);
    void encodeAudio(Job& j);
    void drainVideo();
    void drainAudio();
    void deliver(AVPacket* p, bool video);
    void trimRing();
    void finishRecording();
    void message(const std::string& m, bool error) { if (onMessage) onMessage(m, error); }
    void updateParams();
    bool toNv12(Job& j);
    std::shared_ptr<Params> params_;   // under ringM_
    SwsContext* sws_ = nullptr;
    int swsW_ = 0, swsH_ = 0, swsDW_ = 0, swsDH_ = 0;

    std::mutex qm_;
    std::condition_variable qcv_;
    std::deque<Job> queue_;
    bool quit_ = false;
    std::thread thread_;

    std::atomic<bool> replayOn_{false}, recording_{false};
    std::atomic<int> replaySeconds_{30};
    std::string recPath_;            // pending start (worker picks it up)
    bool recStartPending_ = false, recStopPending_ = false;

    // worker-owned
    AVCodecContext* venc_ = nullptr;
    AVCodecContext* aenc_ = nullptr;
    AVBufferRef* hwDevice_ = nullptr;
    AVBufferRef* hwFrames_ = nullptr;
    AVFrame* swFrame_ = nullptr;
    AVFrame* afr_ = nullptr;
    int vw_ = 0, vh_ = 0;
    uint64_t gen_ = 0;               // bumps when the video encoder is reopened (replay saves only the newest)
    Clock::time_point t0_{};         // timestamp origin of the session
    int64_t lastVPts_ = -1;
    int64_t aSamples_ = -1;          // next audio pts (samples)
    std::vector<float> aPend_[2];    // audio waiting for a full AAC frame
    bool forceKey_ = false;
    std::unique_ptr<Mux> rec_;
    std::unique_ptr<StreamOut> stream_;
    std::atomic<bool> streaming_{false};
    std::string streamUrl_;
    bool streamStartPending_ = false, streamStopPending_ = false;
    bool wantH264_ = false;          // encoder preferences (worker): H.264 for RTMP, fixed bit rate
    int targetKbps_ = 0;
    Clock::time_point streamStart_{};
    std::atomic<double> streamSec_{0};
    void closeVideoEncoder();
    Clock::time_point recStart_{};
    std::atomic<double> recSec_{0};

    mutable std::mutex ringM_;
    std::deque<Pkt> ring_;
    std::string encName_;

    std::vector<std::thread> savers_;
    std::mutex saverM_;
    std::atomic<bool> busySaving_{false};
};

} // namespace veyamux
