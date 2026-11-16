// VeyAmux for Linux: spectrum meter (see AudioMeter.h). Mono mix, Hann window, 1024-point FFT every 512 samples
// (~94 analyses per second at 48 kHz); band level = loudest bin in the band, shown in dB from -66 to -6 dBFS.
#include "AudioMeter.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace veyamux {

namespace {
constexpr double kRate = 48000.0;
constexpr float kFloorDb = -66.0f, kTopDb = -6.0f;

void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * float(M_PI) / float(len);
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k) {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}
} // namespace

AudioMeter::AudioMeter() : ring_(kN, 0.0f), window_(kN) {
    for (int i = 0; i < kN; ++i) window_[size_t(i)] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * float(i) / float(kN - 1));
    const double lo = 50.0, hi = 16000.0, binHz = kRate / kN;
    for (int b = 0; b <= kBands; ++b) {
        const double f = lo * std::pow(hi / lo, double(b) / kBands);
        edges_[size_t(b)] = std::clamp(int(std::lround(f / binHz)), 1, kN / 2 - 1);
    }
    for (int b = 1; b <= kBands; ++b)   // every band at least one bin wide
        edges_[size_t(b)] = std::max(edges_[size_t(b)], edges_[size_t(b - 1)] + 1);
}

void AudioMeter::push(const int16_t* pcm, int frames) {
    std::lock_guard l(m_);
    lastPush_ = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        ring_[size_t(fill_)] = (float(pcm[2 * i]) + float(pcm[2 * i + 1])) * (0.5f / 32768.0f);
        fill_ = (fill_ + 1) % kN;
        if (++sinceLast_ >= kN / 2) { sinceLast_ = 0; analyse(); }
    }
}

void AudioMeter::analyse() {
    std::vector<std::complex<float>> a(kN);
    for (int i = 0; i < kN; ++i) a[size_t(i)] = ring_[size_t((fill_ + i) % kN)] * window_[size_t(i)];
    fft(a);
    bool loud = false;
    for (int b = 0; b < kBands; ++b) {
        float peak = 0.0f;
        for (int k = edges_[size_t(b)]; k < edges_[size_t(b + 1)]; ++k) peak = std::max(peak, std::abs(a[size_t(k)]));
        // a full-scale sine gives |X| = N/4 with a Hann window
        const float db = 20.0f * std::log10(std::max(peak / (kN / 4.0f), 1e-6f));
        const float v = std::clamp((db - kFloorDb) / (kTopDb - kFloorDb), 0.0f, 1.0f);
        raw_[size_t(b)] = std::max(raw_[size_t(b)], v);   // loudest since the last read
        loud = loud || v > 0.08f;
    }
    if (loud) lastSound_ = std::chrono::steady_clock::now();
}

std::array<float, AudioMeter::kBands> AudioMeter::bands() {
    std::lock_guard l(m_);
    const auto now = std::chrono::steady_clock::now();
    const float dt = lastRead_.time_since_epoch().count() ? std::chrono::duration<float>(now - lastRead_).count() : 0.03f;
    lastRead_ = now;
    const bool stale = now - lastPush_ > std::chrono::milliseconds(250);
    const float fall = std::exp(-std::min(dt, 0.2f) / 0.18f);   // ~0.18 s decay
    for (int b = 0; b < kBands; ++b) {
        const float target = stale ? 0.0f : raw_[size_t(b)];
        shown_[size_t(b)] = target > shown_[size_t(b)] ? target : std::max(target, shown_[size_t(b)] * fall);
        raw_[size_t(b)] = 0.0f;
    }
    return shown_;
}

bool AudioMeter::active() {
    std::lock_guard l(m_);
    return std::chrono::steady_clock::now() - lastSound_ < std::chrono::milliseconds(500);
}

} // namespace veyamux
