#include "ContentProbe.h"

#include <cstring>

#include <algorithm>
#include <cmath>
#include <complex>

namespace veyamux {
namespace {
using Clock = std::chrono::steady_clock;

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

float interp(const std::vector<float>& z, float k) {
    const int i = int(std::floor(k));
    const float f = k - float(i);
    if (i < 0) return z.front();
    if (i + 1 >= int(z.size())) return z.back();
    return z[size_t(i)] * (1.0f - f) + z[size_t(i) + 1] * f;
}
} // namespace

ContentProbe::ContentProbe() { thread_ = std::thread([this] { worker(); }); }

ContentProbe::~ContentProbe() {
    { std::lock_guard l(m_); quit_ = true; }
    cv_.notify_all();
    thread_.join();
}

void ContentProbe::reset() {
    std::lock_guard l(m_);
    diff_.reset();
    pw_ = 0;
    all_.clear();
    changed_.clear();
    lastMovingFps_ = 0;
    widths_.clear();
    lastSpectrum_ = {};
}

bool FrameDiff::changed(const uint8_t* luma, int w, int h, int stride, int step, bool keepOnRepeat) {
    if (!luma || w < 16 || h < 16) return true;
    const int sx = std::max(1, w / 480), sy = std::max(1, h / 270);
    const int gw = w / sx, gh = h / sy;
    cur_.resize(size_t(gw) * gh);
    for (int y = 0; y < gh; ++y) {
        const uint8_t* row = luma + size_t(y * sy) * size_t(stride);
        for (int x = 0; x < gw; ++x) cur_[size_t(y) * gw + x] = row[size_t(x * sx) * size_t(step)];
    }
    bool changed = true;
    if (pw_ == gw && ph_ == gh && prev_.size() == cur_.size()) {
        // Mean absolute difference per 4x4 block of samples (32x32 px at 4K). A repeated frame differs only by the
        // card's noise, if any: the median block (almost all blocks are unchanged in a repeat) measures it, and a
        // block counts as changed only clearly above it. Even a small moving element (a cursor, a spinner) changes
        // a few blocks a lot.
        const int bw = gw / 4, bh = gh / 4;
        std::vector<float>& blocks = blocks_;   // kept between frames: no allocation per picture
        blocks.resize(size_t(bw) * bh);
        for (int by = 0; by < bh; ++by)
            for (int bx = 0; bx < bw; ++bx) {
                int s = 0;
                for (int y = 0; y < 4; ++y)
                    for (int x = 0; x < 4; ++x) {
                        const size_t i = size_t(by * 4 + y) * gw + size_t(bx * 4 + x);
                        s += std::abs(int(cur_[i]) - int(prev_[i]));
                    }
                blocks[size_t(by) * bw + bx] = float(s) / 16.0f;
            }
        std::vector<float>& sorted = sorted_;
        sorted.assign(blocks.begin(), blocks.end());
        std::nth_element(sorted.begin(), sorted.begin() + long(sorted.size() / 2), sorted.end());
        const float noise = sorted[sorted.size() / 2];
        const float thr = std::max(1.5f, 3.0f * noise + 1.0f);
        size_t moved = 0;
        for (float bl : blocks) moved += bl > thr;
        // A fade changes every block the same way, which the median would take for noise; noise averages out to
        // zero, a fade does not.
        long long signedSum = 0;
        for (size_t i = 0; i < cur_.size(); ++i) signedSum += int(cur_[i]) - int(prev_[i]);
        const double meanShift = double(signedSum) / double(cur_.size());
        changed = moved >= 2 || std::abs(meanShift) >= 1.0;
    }
    if (changed || !keepOnRepeat || prev_.size() != cur_.size()) { prev_.swap(cur_); pw_ = gw; ph_ = gh; }
    return changed;
}

bool PictureCompare::changed(const uint8_t* luma, int w, int h, int stride, int step) {
    if (!luma || w < 16 || h < 16) { lastDiff_ = 0; return true; }
    const bool fresh = rw_ != w || rh_ != h || ref_.size() != size_t(w) * h;
    const int tol = tolerance(w, h);
    int diff = 0;
    if (!fresh) {
        for (int y = 0; y < h && diff <= tol; ++y) {   // stops at the first row past the tolerance: a new picture
            const uint8_t* s = luma + size_t(y) * size_t(stride);
            const uint8_t* r = ref_.data() + size_t(y) * w;
            int n = 0;
            if (step == 1) {
                // unsigned bytes and a byte counter per 255 samples: the compiler turns this into wide SIMD
                for (int x0 = 0; x0 < w; x0 += 255) {
                    const int x1 = std::min(w, x0 + 255);
                    uint8_t c = 0;
                    for (int x = x0; x < x1; ++x) {
                        const uint8_t a = s[x], b = r[x];
                        const uint8_t d = uint8_t(a > b ? a - b : b - a);
                        c += uint8_t(d > kBand);
                    }
                    n += c;
                }
            } else {
                for (int x = 0; x < w; ++x) {
                    const int d = int(s[size_t(x) * step]) - int(r[x]);
                    n += (d > kBand) | (d < -kBand);
                }
            }
            diff += n;
        }
    }
    lastDiff_ = diff;
    const bool isNew = fresh || diff > tol;
    if (isNew) {   // the reference is the last new picture, so a slow change adds up past the band
        ref_.resize(size_t(w) * h);
        rw_ = w; rh_ = h;
        for (int y = 0; y < h; ++y) {
            const uint8_t* s = luma + size_t(y) * size_t(stride);
            uint8_t* r = ref_.data() + size_t(y) * w;
            if (step == 1) std::memcpy(r, s, size_t(w));
            else for (int x = 0; x < w; ++x) r[x] = s[size_t(x) * step];
        }
    }
    return isNew;
}

void ContentProbe::feed(const uint8_t* luma, int w, int h, int stride, int step, Clock::time_point t, int known) {
    if (!luma || w < 16 || h < 16) return;
    std::lock_guard l(m_);
    const bool changed = known >= 0 ? known != 0 : diff_.changed(luma, w, h, stride, step, true);
    pw_ = w;
    all_.push_back(t);
    if (changed) changed_.push_back(t);
    const auto window = std::chrono::seconds(2);
    while (!all_.empty() && t - all_.front() > window) all_.pop_front();
    while (!changed_.empty() && t - changed_.front() > window) changed_.pop_front();

    // resolution analysis once a second on a block of rows from the middle of the picture (worker thread)
    if (lastSpectrum_ != Clock::time_point{} && t - lastSpectrum_ < std::chrono::seconds(1)) return;
    if (jobReady_ || !changed) return;
    lastSpectrum_ = t;
    int n = 1;
    while (n * 2 <= std::min(w, 2048)) n *= 2;
    const int count = 48;
    job_.resize(size_t(n) * count);
    const int x0 = (w - n) / 2;
    for (int r = 0; r < count; ++r) {
        const int y = h / 5 + (h * 3 / 5) * r / count;
        const uint8_t* row = luma + size_t(y) * size_t(stride);
        for (int x = 0; x < n; ++x) job_[size_t(r) * n + x] = float(row[size_t(x0 + x) * size_t(step)]);
    }
    jobN_ = n; jobCount_ = count; jobW_ = w; jobH_ = h;
    jobReady_ = true;
    cv_.notify_all();
}

void ContentProbe::worker() {
    std::unique_lock l(m_);
    for (;;) {
        cv_.wait(l, [this] { return quit_ || jobReady_; });
        if (quit_) return;
        std::vector<float> rows = job_;
        const int n = jobN_, count = jobCount_, w = jobW_;
        l.unlock();
        const int est = estimateWidth(rows, n, count, w);
        l.lock();
        jobReady_ = false;
        if (est > 0 && w == jobW_) {
            widths_.push_back(est);
            while (widths_.size() > 5) widths_.pop_front();
        }
    }
}

int ContentProbe::estimateWidth(const std::vector<float>& rows, int n, int count, int signalW) {
    const int half = n / 2;
    std::vector<float> win(static_cast<size_t>(n));
    float wsum = 0;
    for (int i = 0; i < n; ++i) { win[size_t(i)] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * float(i) / float(n - 1)); wsum += win[size_t(i)] * win[size_t(i)]; }

    // per-row detrended log spectra
    std::vector<std::vector<float>> logs;
    std::vector<float> avgP(static_cast<size_t>(half), 0.0f);
    std::vector<std::complex<float>> a(static_cast<size_t>(n));
    for (int r = 0; r < count; ++r) {
        const float* row = rows.data() + size_t(r) * n;
        float mean = 0;
        for (int i = 0; i < n; ++i) mean += row[i];
        mean /= float(n);
        for (int i = 0; i < n; ++i) a[size_t(i)] = {(row[i] - mean) * win[size_t(i)], 0.0f};
        fft(a);
        std::vector<float> z(static_cast<size_t>(half));
        float energy = 0;
        for (int k = 1; k < half; ++k) {
            const float p = std::norm(a[size_t(k)]) / wsum;
            avgP[size_t(k)] += p / float(count);
            energy += p;
            z[size_t(k)] = std::log(p + 0.05f);   // 0.05 ~ below 8-bit quantisation noise (1/12)
        }
        z[0] = z[1];
        if (energy / float(half) < 1.0f) continue;   // flat row: nothing to learn
        // remove isolated spectral lines (periodic patterns: HUD grids, text, scanlines) with a 5-bin median, then
        // detrend: what remains is the fine structure of the picture's own detail
        std::vector<float> zm(static_cast<size_t>(half));
        for (int k = 0; k < half; ++k) {
            float v[5]; int c = 0;
            for (int j = std::max(0, k - 2); j <= std::min(half - 1, k + 2); ++j) v[c++] = z[size_t(j)];
            std::nth_element(v, v + c / 2, v + c);
            zm[size_t(k)] = v[c / 2];
        }
        std::vector<float> d(static_cast<size_t>(half));
        for (int k = 0; k < half; ++k) {
            float s = 0; int c = 0;
            for (int j = std::max(0, k - 6); j <= std::min(half - 1, k + 6); ++j) { s += zm[size_t(j)]; ++c; }
            d[size_t(k)] = zm[size_t(k)] - s / float(c);
        }
        logs.push_back(std::move(d));
    }
    if (logs.size() < 8) return 0;

    // An image scaled up from width W keeps a mirror image of its spectrum around W's Nyquist frequency (the
    // interpolation's imaging), whatever the scaler; natural detail has no such symmetry. Scan source widths in 16 px
    // steps (dynamic resolution is rarely a "standard" width), take the lowest one with a clear mirror and an energy drop,
    // then the strongest mirror right around it.
    auto score = [&](int cw, float& corrOut) {
        const float c = float(cw) / float(signalW);
        const float kc = c * float(half);
        const int J = int(0.35f * kc);
        corrOut = 0;
        if (J < 6 || kc + float(J) >= float(half - 2)) return false;
        float eb = 0, ea = 0; int nb = 0, na = 0;
        for (int k = int(0.70f * kc); k <= int(0.92f * kc); ++k) { eb += avgP[size_t(k)]; ++nb; }
        for (int k = int(1.08f * kc); k <= std::min(half - 1, int(1.35f * kc)); ++k) { ea += avgP[size_t(k)]; ++na; }
        if (nb == 0 || na == 0 || eb / float(nb) < 0.5f) return false;
        const float drop = (ea / float(na)) / (eb / float(nb));
        float corrSum = 0;
        for (const auto& d : logs) {
            float sxy = 0, sxx = 0, syy = 0;
            for (int j = 2; j <= J; ++j) {
                // clipped so a few strong spectral lines (periodic HUD patterns, text) cannot dominate
                const float x = std::clamp(interp(d, kc - float(j)), -1.0f, 1.0f);
                const float y = std::clamp(interp(d, kc + float(j)), -1.0f, 1.0f);
                sxy += x * y; sxx += x * x; syy += y * y;
            }
            corrSum += sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : 0.0f;
        }
        corrOut = corrSum / float(logs.size());
        return corrOut > 0.3f && drop < 0.7f;
    };
    const int step = 16;
    for (int cw = 640; cw <= int(0.9f * float(signalW)); cw += step) {
        float corr = 0;
        if (!score(cw, corr)) continue;
        int best = cw; float bestCorr = corr;
        for (int w2 = cw + step; w2 <= std::min(cw + 96, int(0.9f * float(signalW))); w2 += step) {
            float c2 = 0;
            score(w2, c2);
            if (c2 > bestCorr) { bestCorr = c2; best = w2; }
        }
        // report common widths exactly when the estimate is within one step of them
        static const int common[] = {640, 720, 854, 960, 1024, 1152, 1280, 1366, 1440, 1600, 1680, 1920, 2048, 2160, 2304, 2560, 2880, 3200};
        for (int cmn : common)
            if (std::abs(cmn - best) <= step) { best = cmn; break; }
        return best;
    }
    return signalW;
}

ContentProbe::Result ContentProbe::result() const {
    std::lock_guard l(m_);
    Result r;
    auto rate = [](const std::deque<Clock::time_point>& q) {
        if (q.size() < 2) return 0.0;
        const double s = std::chrono::duration<double>(q.back() - q.front()).count();
        return s > 0.2 ? double(q.size() - 1) / s : 0.0;
    };
    r.signalFps = rate(all_);
    const double moving = rate(changed_);
    if (all_.size() >= 10 && changed_.size() < 3) {
        r.staticPicture = true;
        r.contentFps = lastMovingFps_;
    } else {
        r.contentFps = moving;
        if (moving > 0) lastMovingFps_ = moving;
    }
    if (!widths_.empty() && pw_ > 0) {
        std::vector<int> v(widths_.begin(), widths_.end());
        std::nth_element(v.begin(), v.begin() + long(v.size() / 2), v.end());
        r.estW = v[v.size() / 2];
        if (jobW_ > 0) r.estH = int(std::lround(double(r.estW) * jobH_ / jobW_));
    }
    return r;
}

} // namespace veyamux
