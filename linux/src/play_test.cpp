// Headless engine test: veyamux-play-test <shaderDir> <file> [scale]
#include "MediaEngine.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace veyamux;
int main(int argc, char** argv) {
    if (argc < 3) return 2;
    MediaEngine e(argv[1]);
    std::atomic<int> frames{0}; std::atomic<bool> ended{false};
    int lastW = 0, lastH = 0; double aspect = 0;
    MediaEngine::Callbacks cb;
    cb.onFrame = [&](const uint8_t* p, int w, int h, double a) {
        lastW = w; lastH = h; aspect = a;
        if (frames == 20) { FILE* f = std::fopen("/tmp/play_frame.ppm", "wb"); std::fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int i = 0; i < w * h; ++i) std::fwrite(p + i * 4, 1, 3, f); std::fclose(f); }
        ++frames; };
    cb.onEnded = [&] { ended = true; };
    cb.onError = [](const std::string& s) { std::fprintf(stderr, "ERR %s\n", s.c_str()); };
    cb.onGpuMessage = [](const std::string& s) { std::printf("[msg] %s\n", s.c_str()); };
    e.setCallbacks(cb);
    std::string err;
    if (!e.open(argv[2], err)) { std::fprintf(stderr, "open failed: %s\n", err.c_str()); return 1; }
    const auto& i = e.info();
    std::printf("opened: %dx%d %.2ffps v=%s a=%s dur=%.2fs\n", i.width, i.height, i.fps, i.videoCodec.c_str(), i.audioCodec.c_str(), i.duration);
    EnhanceSettings s; s.scale = argc > 3 ? float(std::atof(argv[3])) : 1.0f; s.fxaa = false; s.sharpen = 0.0f; s.frameGen = FrameGenMode::Off;
    e.setEnhance(s);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    std::printf("paused first frame shown: frames=%d\n", frames.load());
    auto t0 = std::chrono::steady_clock::now();
    e.play();
    std::this_thread::sleep_for(std::chrono::seconds(2));
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    auto st = e.stats();
    std::printf("after %.1fs: pos=%.2f frames=%d dropped=%llu proc=%.1fms out=%dx%d gpu=%d hw=%d aspect=%.3f\n",
        el, e.position(), frames.load(), (unsigned long long)st.dropped, st.processMs, lastW, lastH, st.gpuActive, e.hwDecodeActive(), aspect);
    e.pause(); int fp = frames; std::this_thread::sleep_for(std::chrono::milliseconds(300));
    std::printf("pause holds: delta frames=%d\n", frames.load() - fp);
    e.seek(5.0); std::this_thread::sleep_for(std::chrono::milliseconds(700));
    std::printf("seek(5) while paused: pos=%.2f frames=%d\n", e.position(), frames.load());
    e.play();
    for (int k = 0; k < 120 && !ended; ++k) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::printf("ended=%d pos=%.2f total frames=%d\n", ended.load(), e.position(), frames.load());
    e.close();
    return ended ? 0 : 3;
}
