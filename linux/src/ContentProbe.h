// VeyAmux for Linux - what is actually inside a capture-card signal.
//
// A capture card delivers a fixed signal (e.g. 3840x2160 @ 60 Hz) whatever the console or PC renders: a game running at
// 30 fps repeats every frame twice, and a game rendering 1080p is scaled up to 4K before it reaches the card. This probe
// looks at the frames themselves:
//  * content frame rate: frames whose picture differs from the previous one, per second (exact, cheap: a sparse luma
//    grid is compared for every frame the driver delivers, including the ones dropped for latency);
//  * content resolution (estimate): an upscaled picture has (almost) no detail above the source resolution's Nyquist
//    frequency. Once a second a block of rows is analysed on a worker thread (FFT power spectrum); a sharp drop of
//    energy at the cut-off of a common resolution (1280, 1600, 1920, 2560 wide...) means the game renders at that
//    width. In-game sharpening or temporal upscalers (FSR/DLSS/checkerboarding) recreate fine detail, so then the
//    estimate reads higher; a flat scene gives no estimate. It is shown with "~".
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace veyamux {

// Is this frame a different picture from the reference frame? Compares a sparse luma grid (~480x270 samples) in 4x4
// sample blocks against the card's own noise level (median block). keepOnRepeat: the reference stays the last
// *different* frame, so slow fades still register once they add up.
class FrameDiff {
public:
    bool changed(const uint8_t* luma, int w, int h, int stride, int step, bool keepOnRepeat);
    void reset() { prev_.clear(); }
private:
    std::vector<uint8_t> prev_, cur_;
    std::vector<float> blocks_, sorted_;   // scratch, kept between frames
    int pw_ = 0, ph_ = 0;
};

class ContentProbe {
public:
    struct Result {
        double contentFps = 0;     // distinct pictures per second (0 = not measured yet)
        double signalFps = 0;      // frames per second the card delivered
        bool staticPicture = false;// nothing changed recently (paused game, menu): contentFps is the last moving value
        int estW = 0, estH = 0;    // estimated rendering resolution (0 = unknown)
    };

    ContentProbe();
    ~ContentProbe();
    ContentProbe(const ContentProbe&) = delete;
    ContentProbe& operator=(const ContentProbe&) = delete;

    void reset();
    // One delivered frame. Luma sample (x, y) is luma[y * stride + x * step]. t = capture timestamp.
    void feed(const uint8_t* luma, int w, int h, int stride, int step, std::chrono::steady_clock::time_point t);
    Result result() const;

    // Resolution estimate for one block of luma rows (each `n` samples, n a power of two); exposed for tests.
    // Returns the estimated source width relative to `signalW`, or 0 when the block has too little detail.
    static int estimateWidth(const std::vector<float>& rows, int n, int count, int signalW);

private:
    void worker();
    mutable std::mutex m_;
    FrameDiff diff_;
    int pw_ = 0;
    std::deque<std::chrono::steady_clock::time_point> all_, changed_;
    mutable double lastMovingFps_ = 0;
    std::deque<int> widths_;      // recent per-second estimates (median shown)
    std::chrono::steady_clock::time_point lastSpectrum_{};
    // worker
    std::thread thread_;
    std::condition_variable cv_;
    bool quit_ = false, jobReady_ = false;
    std::vector<float> job_;
    int jobN_ = 0, jobCount_ = 0, jobW_ = 0, jobH_ = 0;
};

} // namespace veyamux
