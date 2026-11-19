// VeyAmux for Linux: the demo scene as a source (see DemoSource.h).
#include "DemoSource.h"
#include "CaptureSource.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

#if VEYAMUX_HAVE_ABYSSAL
#include "AbyssalAudio.h"
#include "AbyssalScene.h"
#endif

namespace veyamux::demo {

#if VEYAMUX_HAVE_ABYSSAL
namespace {

using clk = std::chrono::steady_clock;

struct Engine {
    std::mutex m;
    std::condition_variable cv;        // a new picture, or a change of fps / stop
    std::thread th;
    bool stop = false;
    int fps = 0;                       // 0: no stream open, nothing is rendered
    int reqW = 1920, reqH = 1080;      // the size the open stream asked for
    int frameW = 0, frameH = 0;        // the size of `frame`
    double prog = 0;
    std::string stageText;
    bool failed = false;
    std::vector<uint8_t> frame, spare;
    uint64_t serial = 0;
    clk::time_point frameTime{};

    // input
    std::mutex im;
    bool keys[7]{};
    double lookX = 0, lookY = 0;
    double pad[6]{};                   // lx, ly, rx, ry, down, up
    bool padFast = false;
    int pose = -1;

    abyssal::Synth synth;

    void publish(std::vector<uint8_t>& px, int w, int h) {
        std::lock_guard l(m);
        frame.swap(px);
        frameW = w; frameH = h;
        serial++;
        frameTime = clk::now();
        cv.notify_all();
    }
    // the picture while the world is built: dark water with a progress bar
    void loadingFrame(double p) {
        int kWidth, kHeight;
        { std::lock_guard l(m); kWidth = reqW; kHeight = reqH; }
        std::vector<uint8_t> px(size_t(kWidth) * kHeight * 4);
        for (int y = 0; y < kHeight; y++) {
            const double g = double(y) / kHeight;
            const uint8_t r = uint8_t(4 + 10 * (1 - g)), gg = uint8_t(22 + 40 * (1 - g)), b = uint8_t(34 + 50 * (1 - g));
            uint8_t* row = px.data() + size_t(y) * kWidth * 4;
            for (int x = 0; x < kWidth; x++) { row[x * 4] = r; row[x * 4 + 1] = gg; row[x * 4 + 2] = b; row[x * 4 + 3] = 255; }
        }
        const int bw = kWidth / 3, bh = std::max(6, kHeight / 108), bx = (kWidth - bw) / 2, by = kHeight / 2 + 40;
        for (int y = by; y < by + bh; y++)
            for (int x = bx; x < bx + bw; x++) {
                uint8_t* q = px.data() + (size_t(y) * kWidth + x) * 4;
                const bool on = x - bx < int(bw * std::clamp(p, 0.0, 1.0));
                q[0] = on ? 120 : 20; q[1] = on ? 220 : 50; q[2] = on ? 230 : 64;
            }
        publish(px, kWidth, kHeight);
    }
    abyssal::Controls controls() {
        std::lock_guard l(im);
        abyssal::Controls c;
        const auto dz = [](double v) { return std::abs(v) < 0.15 ? 0.0 : (v - std::copysign(0.15, v)) / 0.85; };
        c.forward = std::clamp(double(keys[0]) - keys[1] - dz(pad[1]), -1.0, 1.0);
        c.right = std::clamp(double(keys[3]) - keys[2] + dz(pad[0]), -1.0, 1.0);
        c.up = std::clamp(double(keys[4]) - keys[5] + dz(pad[5]) - dz(pad[4]), -1.0, 1.0);
        c.fast = keys[6] || padFast;
        c.lookX = lookX; c.lookY = lookY; lookX = lookY = 0;
        c.stickX = dz(pad[2]); c.stickY = dz(pad[3]);
        c.pose = pose; pose = -1;
        return c;
    }
    void run() {
        abyssal::Scene scene;
        std::string err;
        int w0, h0;
        { std::lock_guard l(m); w0 = reqW; h0 = reqH; }
        const bool ok = scene.init(w0, h0, [this](const char* what, double p) {
            { std::lock_guard l(m); prog = std::min(p, 0.99); stageText = what; }
            loadingFrame(p);
        }, err);
        {
            std::lock_guard l(m);
            if (!ok) { failed = true; stageText = err; cv.notify_all(); return; }
            prog = 1; stageText = scene.adapterName();
        }
        std::vector<uint8_t> px;
        clk::time_point last = clk::now(), next = last;
        for (;;) {
            int f, rw, rh;
            {
                std::unique_lock l(m);
                cv.wait(l, [&] { return stop || fps > 0; });
                if (stop) return;
                f = fps; rw = reqW; rh = reqH;
            }
            if ((rw != scene.width() || rh != scene.height()) && !scene.resize(rw, rh)) {
                std::lock_guard l(m);
                failed = true; stageText = "The demo could not change its picture size"; cv.notify_all();
                return;
            }
            const clk::time_point now = clk::now();
            double dt = std::chrono::duration<double>(now - last).count();
            if (dt > 0.25) dt = 1.0 / f;   // after a pause: no jump
            last = now;
            if (!scene.frame(dt, controls(), px)) {
                std::lock_guard l(m);
                failed = true; stageText = "The demo's graphics device was lost"; cv.notify_all();
                return;
            }
            double x, y, z;
            scene.camera(x, y, z);
            synth.setCamera(x, y, z);
            publish(px, scene.width(), scene.height());
            // pace to the chosen frame rate
            next += std::chrono::nanoseconds(int64_t(1e9 / f));
            const auto after = clk::now();
            if (next < after - std::chrono::milliseconds(50)) next = after;   // too slow: don't try to catch up
            std::unique_lock l(m);
            cv.wait_until(l, next, [&] { return stop; });
            if (stop) return;
        }
    }
};

Engine* engine() {
    static Engine* e = nullptr;
    static std::once_flag once;
    std::call_once(once, [] {
        e = new Engine;
        e->th = std::thread([] { engine()->run(); });
    });
    return e;
}
std::atomic<bool> started{false};
Engine* existing() { return started.load() ? engine() : nullptr; }

} // namespace

bool available() { return true; }

void setKey(Key k, bool down) { Engine* e = existing(); if (!e) return; std::lock_guard l(e->im); e->keys[int(k)] = down; }
void addLook(double dx, double dy) { Engine* e = existing(); if (!e) return; std::lock_guard l(e->im); e->lookX += dx; e->lookY += dy; }
void setPad(double lx, double ly, double rx, double ry, double down, double up, bool fast) {
    Engine* e = existing(); if (!e) return;
    std::lock_guard l(e->im);
    e->pad[0] = lx; e->pad[1] = ly; e->pad[2] = rx; e->pad[3] = ry; e->pad[4] = down; e->pad[5] = up; e->padFast = fast;
}
void jumpTo(int pose) { Engine* e = existing(); if (!e) return; std::lock_guard l(e->im); e->pose = pose; }
void releaseAll() {
    Engine* e = existing(); if (!e) return;
    std::lock_guard l(e->im);
    std::fill(std::begin(e->keys), std::end(e->keys), false);
    std::fill(std::begin(e->pad), std::end(e->pad), 0.0);
    e->padFast = false;
}
double progress() { Engine* e = existing(); if (!e) return 0; std::lock_guard l(e->m); return e->prog; }
std::string stage() { Engine* e = existing(); if (!e) return {}; std::lock_guard l(e->m); return e->stageText; }
bool failed() { Engine* e = existing(); if (!e) return false; std::lock_guard l(e->m); return e->failed; }
void audio(int16_t* out, int frames) {
    Engine* e = existing();
    if (!e) { std::memset(out, 0, size_t(frames) * 4); return; }
    e->synth.render(out, frames);
}

Stream::~Stream() { close(); }

bool Stream::open(int fps, int height, std::string& error) {
    close();
    height_ = heightFor(height);
    {   // the size first, so even the loading picture has it
        Engine* e = engine();
        std::lock_guard l(e->m);
        e->reqW = widthFor(height_); e->reqH = height_;
    }
    started = true;
    Engine* e = engine();
    std::lock_guard l(e->m);
    if (e->failed) { error = e->stageText; return false; }
    fps_ = fps == 30 ? 30 : 60;
    e->fps = fps_;
    e->frame.clear();   // a picture of the old size is not handed out
    seen_ = 0;
    open_ = true;
    e->cv.notify_all();
    return true;
}

void Stream::close() {
    if (!open_) return;
    open_ = false;
    Engine* e = engine();
    std::lock_guard l(e->m);
    e->fps = 0;   // pause rendering; the world stays built
    e->cv.notify_all();
}

Stream::Result Stream::readLatest(RawFrame& out, int timeoutMs, std::string& error) {
    Engine* e = engine();
    std::unique_lock l(e->m);
    if (!e->cv.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return e->serial > seen_ || e->failed || e->stop; })) return Result::Timeout;
    seen_ = e->serial;
    if (e->frameW != width() || e->frameH != height_) return Result::Timeout;   // still the previous size
    if (e->failed) { error = e->stageText; return Result::Error; }
    if (e->stop) { error = "The demo stopped"; return Result::Error; }
    out.fmt = RawFrame::RGBA;
    out.w = e->frameW; out.h = e->frameH; out.stride = e->frameW * 4;
    out.repeat = false;
    out.captured = e->frameTime;
    out.data.swap(e->frame);   // the producer allocates a new one
    if (out.data.empty()) return Result::Timeout;
    return Result::Frame;
}

void shutdown() {
    Engine* e = existing();
    if (!e) return;
    { std::lock_guard l(e->m); e->stop = true; e->cv.notify_all(); }
    if (e->th.joinable()) e->th.join();
}

#else // built without wgpu-native

bool available() { return false; }
void setKey(Key, bool) {}
void addLook(double, double) {}
void setPad(double, double, double, double, double, double, bool) {}
void jumpTo(int) {}
void releaseAll() {}
double progress() { return 0; }
std::string stage() { return {}; }
bool failed() { return true; }
void audio(int16_t* out, int frames) { std::memset(out, 0, size_t(frames) * 4); }
Stream::~Stream() = default;
bool Stream::open(int, int, std::string& error) { error = "This build has no demo scene"; return false; }
void Stream::close() {}
Stream::Result Stream::readLatest(RawFrame&, int, std::string& error) { error = "This build has no demo scene"; return Result::Error; }
void shutdown() {}

#endif

} // namespace veyamux::demo
