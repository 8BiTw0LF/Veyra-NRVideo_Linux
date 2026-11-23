// VeyAmux for Linux - V4L2 capture-card input (UVC HDMI capture dongles, Elgato/AVerMedia/etc. in UVC mode).
#pragma once
#include <chrono>
#include "ContentProbe.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct SwsContext;
struct v4l2_buffer;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;

namespace veyamux { class ScreenCastStream; }
namespace veyamux::demo { class Stream; }
namespace veyamux::remoteplay { class Stream; }

namespace veyamux {

struct CaptureDevice {
    std::string path;   // /dev/videoN
    std::string name;   // V4L2 card name
};

struct CaptureMode {
    uint32_t fourcc = 0;
    int w = 0, h = 0;
    double fps = 0;
    uint32_t num = 1, den = 30;   // frame interval (num/den seconds)
    std::string label;            // "1920x1080 @ 60 fps · YUYV"
};

std::vector<CaptureDevice> listCaptureDevices();
// Sorted best-first: >=50 fps first, then resolution, then fps, uncompressed before MJPEG.
std::vector<CaptureMode> listCaptureModes(const std::string& path);
// the order the list shows (the first is the default): see CaptureSource.cpp
void orderCaptureModes(std::vector<CaptureMode>& modes);
std::string fourccName(uint32_t fourcc);

// Converts one captured buffer to RGBA8 (tightly packed).
class FrameConverter {
public:
    FrameConverter();
    ~FrameConverter();
    bool init(uint32_t fourcc, int w, int h, int stride, std::string& error);
    bool convert(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, std::string& error);
    void setFullRange(bool v) { fullRange_ = v; }
private:
    uint32_t fourcc_ = 0; int w_ = 0, h_ = 0, stride_ = 0; bool fullRange_ = false;
    int pixFmt_ = -1;
    SwsContext* sws_ = nullptr;
    AVCodecContext* mjpeg_ = nullptr;
    AVFrame* frame_ = nullptr;
    AVPacket* pkt_ = nullptr;
    std::vector<uint8_t> packet_;   // the JPEG with the zero padding FFmpeg's decoders may read past the end
};

// A captured frame ready for the GPU. YUYV/UYVY/NV12/P010 are passed through untouched (converted on the GPU);
// MJPEG and the remaining formats are decoded to RGBA on the capture thread. P010 = 10-bit HDR (PQ, BT.2020).
struct RawFrame {
    enum Fmt { RGBA, YUYV, UYVY, NV12, P010 } fmt = RGBA;
    std::vector<uint8_t> data;
    int w = 0, h = 0, stride = 0;     // stride in bytes
    bool repeat = false;              // same picture as the previous delivered frame (game slower than the signal)
    bool reset = false;               // a jump in the timeline (video file seek): temporal passes start over
    bool fullRange = false;           // YUV levels 0-255 (the card says so) instead of the usual 16-235
    // Capture cards (V4L2, raw formats): the picture is not copied out of the driver's buffer. `ext` points into it and
    // `hold` keeps it from the driver until the frame is dropped (then it is queued again), so the renderer copies it
    // once, straight into the GPU's upload buffer. `data` is unused then.
    const uint8_t* ext = nullptr;
    size_t extSize = 0;
    std::shared_ptr<void> hold;
    const uint8_t* bytes() const { return ext ? ext : data.data(); }
    size_t size() const { return ext ? extSize : data.size(); }
    std::chrono::steady_clock::time_point captured{};   // when the driver finished receiving it (V4L2 timestamp)
    std::chrono::steady_clock::time_point due{};        // paced frames: when the pacer meant to hand it on (timing log)
    std::chrono::steady_clock::time_point arrived{};    // when it reached the renderer (timing log)
};

class CaptureSource {
public:
    enum class Result { Frame, Timeout, Error };
    CaptureSource();
    ~CaptureSource();
    // path: /dev/videoN, or "pipewire:<fd>:<node>" for a window/screen shared through the portal (see ScreenCast.h;
    // mode is ignored then).
    bool open(const std::string& path, const CaptureMode& mode, std::string& error);
    static bool isScreenCast(const std::string& path) { return path.rfind("pipewire:", 0) == 0; }
    // "demo:30" / "demo:60": the built-in demo scene (DemoSource.h)
    static bool isDemo(const std::string& path) { return path.rfind("demo:", 0) == 0; }
    // "ps:<console id>": PlayStation Remote Play (RemotePlay.h)
    static bool isRemotePlay(const std::string& path) { return path.rfind("ps:", 0) == 0; }
    void close();
    Result read(std::vector<uint8_t>& rgba, int timeoutMs, std::string& error);
    // Newest frame only; anything older that the driver queued is dropped immediately (lowest latency).
    Result readLatest(RawFrame& out, int timeoutMs, std::string& error);
    int width() const { return w_; }
    int height() const { return h_; }
    double fps() const { return fps_; }
    const std::string& cardName() const { return card_; }
    void setFullRange(bool v) { conv_.setFullRange(v); }
    // What the signal actually carries (content frame rate, estimated rendering resolution).
    ContentProbe::Result content() const { return probe_.result(); }
    // read(): was the frame just returned a repeat of the previous one?
    bool lastWasRepeat() const { return lastRepeat_; }
private:
    struct Buf { void* start = nullptr; size_t length = 0; };
    bool dequeueNewest(v4l2_buffer& held, std::string& error);
    int fd_ = -1;
    std::unique_ptr<ScreenCastStream> sc_;   // window / screen source
    std::unique_ptr<demo::Stream> demo_;     // demo scene
    std::unique_ptr<remoteplay::Stream> ps_; // PlayStation Remote Play
    FrameConverter psConv_;                  // its picture as RGBA (software path)
    int psConvW_ = 0, psConvH_ = 0, psConvFmt_ = -1;
    uint32_t fourcc_ = 0;
    int rawStride_ = 0;
    bool fullRange_ = false;          // the card reports full-range YUV (V4L2 quantization)
    std::vector<Buf> bufs_;
    // the mappings and the device, shared with the frames that still hold a buffer (RawFrame::hold): unmapped when
    // the last one is gone; a buffer handed back after close() is not queued again
    struct Mapping {
        std::mutex m;
        int fd = -1;
        bool live = true;
        int held = 0;                 // buffers handed out to frames (RawFrame::hold) and not yet queued again
        std::vector<Buf> bufs;
        ~Mapping();
    };
    std::shared_ptr<Mapping> map_;
    bool streaming_ = false;
    int w_ = 0, h_ = 0; double fps_ = 0; std::string card_;
    FrameConverter conv_;
    ContentProbe probe_;
    FrameDiff delivered_;             // repeat detection against the last frame handed out (compressed / RGBA)
    PictureCompare rawCompare_;       // uncompressed formats: every buffer the card delivers against the last new picture
    bool newSinceDelivered_ = true;   // a new picture came since the last frame handed out (it is not a repeat)
    bool lastRepeat_ = false;
    void probeRaw(const v4l2_buffer& b);
    void probeRgba(const std::vector<uint8_t>& rgba);
};

} // namespace veyamux
