// VeyAmux for Linux: a small spectrum meter of the source's sound (what the capture card / file delivers, before
// volume and mute), so the menu can show whether sound is coming in at all.
#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

namespace veyamux {

class AudioMeter {
public:
    static constexpr int kBands = 24;   // log-spaced, 50 Hz .. 16 kHz
    AudioMeter();
    // 48 kHz interleaved stereo s16 (audio thread)
    void push(const int16_t* pcm, int frames);
    // 0..1 per band, smoothed (fast rise, slow fall); falls to 0 when no sound arrives (any thread)
    std::array<float, kBands> bands();
    // true while sound above the noise floor arrived within the last ~0.5 s
    bool active();

private:
    static constexpr int kN = 1024;
    void analyse();
    std::mutex m_;
    std::vector<float> ring_;
    int fill_ = 0, sinceLast_ = 0;
    std::array<float, kBands> raw_{}, shown_{};
    std::array<int, kBands + 1> edges_{};   // FFT bin borders of the bands
    std::vector<float> window_;
    std::chrono::steady_clock::time_point lastSound_{}, lastPush_{}, lastRead_{};
};

} // namespace veyamux
