// VeyAmux for Linux: Abyssal Garden demo scene, audio.js (see AbyssalAudio.h).
#include "AbyssalAudio.h"
#include "AbyssalMath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <mutex>
#include <vector>

namespace abyssal {

namespace {

// biquad filter as Web Audio defines it (RBJ cookbook; lowpass Q in dB, bandpass Q linear)
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    void lowpass(double f, double qdb, double rate) {
        const double w = 2 * PI * f / rate, q = std::pow(10.0, qdb / 20), al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
        b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void bandpass(double f, double q, double rate) {
        const double w = 2 * PI * f / rate, al = std::sin(w) / (2 * q), c = std::cos(w), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    double operator()(double x) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};
// AudioParam.setTargetAtTime: exponential approach
struct Smooth {
    double v = 0, target = 0, tau = 0.1;
    void to(double t, double tc) { target = t; tau = tc; }
    double step(double dt) { v += (target - v) * (1 - std::exp(-dt / std::max(tau, 1e-4))); return v; }
};
struct Comb {
    std::vector<float> buf; size_t i = 0; float fb = 0.84f, damp = 0.4f, store = 0;
    float operator()(float x) { const float y = buf[i]; store = y * (1 - damp) + store * damp; buf[i] = x + store * fb; if (++i >= buf.size()) i = 0; return y; }
};
struct Allpass {
    std::vector<float> buf; size_t i = 0;
    float operator()(float x) { const float b = buf[i]; const float y = -x + b; buf[i] = x + b * 0.5f; if (++i >= buf.size()) i = 0; return y; }
};

} // namespace

struct Synth::Impl {
    double rate;
    Rng rng;
    // seamless brown noise loop (11 s, stereo)
    std::vector<float> nb[2];
    double pos1 = 0, pos063 = 0;
    Biquad rumLP[2], waveBP[2], rushBP[2];
    double t = 0;   // audio clock (s)
    // drone: stacked fifths on D, each note a pair of slightly detuned sines
    struct Osc { double f, ph = 0; };
    std::vector<std::pair<Osc, double>> pad;   // oscillator, gain
    // pooled voices
    struct Voice {
        double t0 = -1, f0 = 0, f1 = 0, dur = 1, vol = 0, att = 0.01, pan = 0, ph = 0, send = 0;
        bool tri = false;
    };
    std::array<Voice, 4> bub; std::array<Voice, 3> chime; std::array<Voice, 2> whale;
    size_t nBub = 0, nChime = 0, nWhale = 0;
    double tB, tC, tW;
    Smooth rush, rushF, wash, master;
    double rushFreqSet = 300;
    // camera (from the render thread)
    std::mutex camM;
    double cx = 0, cy = -40, cz = 0;
    bool camSet = false;
    double lx = 0, ly = 0, lz = 0, lt = 0;
    double tick = 0;
    // reverb (left / right)
    std::array<Comb, 8> combs[2];
    std::array<Allpass, 4> aps[2];
    double lim = 1;

    explicit Impl(int r) : rate(r) {
        rng.reseed(300);
        const int n = int(rate * 11);
        for (auto& ch : nb) {
            ch.assign(n, 0.0f);
            double b = 0;
            for (int i = 0; i < n + 4096; i++) {
                b = (b + 0.02 * (rng() * 2 - 1)) / 1.02;
                if (i < n) ch[i] = float(b * 3.5);
                else { const double k = double(i - n) / 4096; ch[i - n] = float(ch[i - n] * k + b * 3.5 * (1 - k)); }
            }
        }
        for (auto& f : rumLP) f.lowpass(150, 0.7, rate);
        for (auto& f : waveBP) f.bandpass(420, 0.5, rate);
        for (auto& f : rushBP) f.bandpass(300, 0.9, rate);
        const double notes[] = {73.42, 110, 164.81, 246.94, 369.99};
        for (int i = 0; i < 5; i++) {
            const double g = 0.022 - i * 0.003;
            pad.push_back({{notes[i]}, g});
            pad.push_back({{notes[i] + rng.rr(0.03, 0.11)}, g});
        }
        for (auto& v : whale) v.tri = true;
        tB = 1.5; tC = 5; tW = 12;
        wash.v = 0.25; wash.target = 0.25;
        master.to(0.78, 1);   // VOL: about -18.5 LUFS
        const int combLen[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617}, apLen[4] = {556, 441, 341, 225};
        for (int c = 0; c < 2; c++) {
            for (int i = 0; i < 8; i++) {
                combs[c][i].buf.assign(size_t((combLen[i] + c * 23) * rate / 44100.0), 0.0f);
                combs[c][i].fb = 0.9f;    // ~4.6 s decay
                combs[c][i].damp = 0.45f; // darker as it fades
            }
            for (int i = 0; i < 4; i++) aps[c][i].buf.assign(size_t((apLen[i] + c * 23) * rate / 44100.0), 0.0f);
        }
    }

    // one note: pitch glides f0 -> f1 over dur, level rises to vol in att then dies away
    template <size_t N> void play(std::array<Voice, N>& vs, size_t& next, double when, double f0, double f1, double dur, double vol, double att, double send) {
        Voice& v = vs[next++ % N];
        const bool tri = v.tri;
        v = Voice{};
        v.tri = tri;
        v.t0 = when; v.f0 = f0; v.f1 = f1; v.dur = dur; v.vol = vol; v.att = att; v.pan = rng.rr(-0.7, 0.7); v.send = send;
    }
    double voice(Voice& v) {
        if (v.t0 < 0 || t < v.t0) return 0;
        const double u = t - v.t0;
        const double f = u < v.dur ? v.f0 * std::pow(v.f1 / v.f0, u / v.dur) : v.f1;
        const double g = u < v.att ? v.vol * u / v.att : v.vol * std::exp(-(u - v.att) / std::max((v.dur - v.att) / 5, 1e-3));
        if (u > v.dur * 3 + 1) { v.t0 = -1; return 0; }
        v.ph += f / rate;
        v.ph -= std::floor(v.ph);
        const double s = v.tri ? 1 - 4 * std::abs(v.ph - 0.5) : std::sin(2 * PI * v.ph);
        return s * g;
    }
    // the 10 Hz control timer of the original
    void control() {
        double x, y, z;
        { std::lock_guard l(camM); x = cx; y = cy; z = cz; if (!camSet) return; }
        const double now = t, tt0 = now + 0.05, dt = now - lt;
        if (dt <= 0) return;
        double v = hyp(x - lx, y - ly, z - lz) / dt;
        if (v > 60 || lt == 0) v = 0;   // ignore jumps to a viewpoint
        lt = now; lx = x; ly = y; lz = z;
        rush.to(0.5 * std::pow(std::min(v / 26, 1.0), 1.2), 0.3);
        rushF.to(220 + v * 14, 0.3);
        const double s = clampd(1 + ly / 30, 0, 1);
        wash.to(0.25 + 0.55 * s * s, 0.5);
        if (tt0 > tB) { // a burst of 1-6 bubbles: short rising blips, smaller bubbles higher and shorter
            double tt = tt0;
            for (int n = toI32(1 + rng() * rng() * 6); n--;) {
                const double f = rng.rr(380, 1300), f1 = f * rng.rr(1.4, 2.2), vol = rng.rr(0.03, 0.08);
                play(bub, nBub, tt, f, f1, 0.05 + 40 / f, vol, 0.006, 0.12);
                tt += rng.rr(0.07, 0.2);
            }
            tB = tt + rng.rr(0.5, 5);
        }
        if (tt0 > tC) {
            static const int NOTES[] = {0, 2, 5, 7, 9, 12, 14, 17, 19};   // D major pentatonic, semitones above A4
            const double f = 440 * std::pow(2.0, NOTES[toI32(rng() * 9)] / 12.0);
            play(chime, nChime, tt0, f, f, 5, rng.rr(0.02, 0.04), 0.03, 1.2);
            tC = tt0 + rng.rr(2.5, 9);
        }
        if (tt0 > tW) { // a phrase of 2-3 slow glides
            double tt = tt0;
            for (int n = 2 + toI32(rng() * 2); n--;) {
                const double f = rng.rr(170, 380), d = rng.rr(1.6, 3.2), f1 = f * rng.rr(0.6, 1.7);
                play(whale, nWhale, tt, f, f1, d, 0.08, d * 0.4, 2);
                tt += d * 0.8;
            }
            tW = tt + rng.rr(20, 50);
        }
    }
    double noise(int ch, double& pos, double rateMul) {
        const auto& b = nb[ch];
        const size_t n = b.size();
        const size_t i = size_t(pos);
        const double fr = pos - double(i);
        const double s = b[i % n] * (1 - fr) + b[(i + 1) % n] * fr;
        return s;
        (void)rateMul;
    }
    void render(int16_t* out, int frames) {
        const double dt = 1.0 / rate;
        for (int i = 0; i < frames; i++) {
            if ((tick += dt) >= 0.1) { tick -= 0.1; control(); }
            t += dt;
            const double mg = master.step(dt), rg = rush.step(dt), rf = rushF.step(dt), wg = wash.step(dt);
            if (std::abs(rf - rushFreqSet) > 2) { rushFreqSet = rf; for (auto& f : rushBP) f.bandpass(rf, 0.9, rate); }
            // LFOs
            const double rumG = 0.34 + 0.11 * std::sin(2 * PI * 0.07 * t) + 0.08 * std::sin(2 * PI * 0.113 * t);
            const double waveG = 1 + 0.3 * std::sin(2 * PI * 0.09 * t) + 0.25 * std::sin(2 * PI * 0.151 * t);
            double L = 0, R = 0, wetIn = 0, padS = 0;
            for (auto& [o, g] : pad) { padS += std::sin(2 * PI * o.ph) * g; o.ph += o.f / rate; o.ph -= std::floor(o.ph); }
            wetIn += padS * 1.5;
            double dryV[2] = {0, 0};
            const auto mixVoices = [&](auto& vs) {
                for (auto& v : vs) {
                    const double s = voice(v);
                    if (s == 0) continue;
                    const double x = (v.pan + 1) / 2;
                    dryV[0] += s * std::cos(x * PI / 2); dryV[1] += s * std::sin(x * PI / 2);
                    wetIn += s * v.send;
                }
            };
            mixVoices(bub); mixVoices(chime); mixVoices(whale);
            const size_t n = nb[0].size();
            double ch[2];
            for (int c = 0; c < 2; c++) {
                const double n1 = noise(c, pos1, 1), n2 = noise(c, pos063, 0.63);
                const double rum = rumLP[c](n1) * rumG;
                const double wave = waveBP[c](n2) * waveG * wg;
                const double rsh = rushBP[c](n2) * rg;
                ch[c] = rum + wave + rsh + padS + dryV[c];
            }
            pos1 += 1; if (pos1 >= n) pos1 -= n;
            pos063 += 0.63; if (pos063 >= n) pos063 -= n;
            // reverb
            for (int c = 0; c < 2; c++) {
                float acc = 0;
                const float in = float(wetIn * 0.015);
                for (auto& cb : combs[c]) acc += cb(in);
                for (auto& ap : aps[c]) acc = ap(acc);
                ch[c] += acc * 0.9;
            }
            L = ch[0] * mg; R = ch[1] * mg;
            // limiter (threshold -6 dBFS, fast attack, 0.3 s release)
            const double peak = std::max(std::abs(L), std::abs(R)) * lim;
            if (peak > 0.5) lim *= 0.5 / peak;
            lim += (1 - lim) * (1 - std::exp(-dt / 0.3));
            L *= lim; R *= lim;
            out[i * 2] = int16_t(std::clamp(L * 32767.0, -32768.0, 32767.0));
            out[i * 2 + 1] = int16_t(std::clamp(R * 32767.0, -32768.0, 32767.0));
        }
    }
};

Synth::Synth(int rate) : d(std::make_unique<Impl>(rate)) {}
Synth::~Synth() = default;
void Synth::render(int16_t* out, int frames) { d->render(out, frames); }
void Synth::setCamera(double x, double y, double z) {
    std::lock_guard l(d->camM);
    d->cx = x; d->cy = y; d->cz = z; d->camSet = true;
}

} // namespace abyssal
