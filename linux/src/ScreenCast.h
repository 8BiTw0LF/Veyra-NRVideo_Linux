// VeyAmux for Linux: a window or a whole screen as the source (instead of a capture card).
//
// The compositor's screen-share dialog (xdg-desktop-portal ScreenCast; KDE, GNOME, ...) lets the user pick what to
// share and hands back a PipeWire remote (an fd) plus the node id of the video stream. ScreenCastStream reads that
// stream: frames arrive as shared-memory RGBA/BGRA (only when the picture changes) and the newest one is handed to the
// same live path as a capture card. libpipewire is loaded from the system at run time (it must match the system's
// PipeWire daemon and plug-ins, so the AppImage does not carry its own).
#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace veyamux {

struct RawFrame;

// ---- the stream (any thread; used by CaptureSource on the capture thread)
class ScreenCastStream {
public:
    enum class Result { Frame, Timeout, Error };
    ScreenCastStream();
    ~ScreenCastStream();
    // fd: PipeWire remote from the portal (dup'ed here), or -1 for the default PipeWire daemon (tests).
    // Waits up to ~5 s for the first frame so the size is known.
    bool open(int fd, uint32_t node, std::string& error);
    void close();
    // Newest frame as RGBA (fmt RGBA, any stride). Timeout = no new picture yet (nothing changed on screen).
    Result readLatest(RawFrame& out, int timeoutMs, std::string& error);
    int width() const { return w_; }
    int height() const { return h_; }
    double fps() const { return fps_; }

    static bool available(std::string* why = nullptr);   // libpipewire-0.3 loadable

    // called on the PipeWire thread
    void onFormat(const void* pod);
    void onProcess();
    void onState(int state, const char* error);

private:
    struct Impl;
    Impl* d_ = nullptr;
    int w_ = 0, h_ = 0;
    double fps_ = 60;

    std::mutex m_;
    std::condition_variable cv_;
    std::vector<uint8_t> latest_;   // newest finished picture (under m_)
    std::vector<uint8_t> spare_;    // PipeWire thread: the picture being written
    int lw_ = 0, lh_ = 0;
    bool fresh_ = false;
    bool failed_ = false;
    std::string failMsg_;
    std::chrono::steady_clock::time_point stamp_{};
    // negotiated format (PipeWire thread)
    uint32_t fmt_ = 0;
    int fw_ = 0, fh_ = 0;
};

} // namespace veyamux
