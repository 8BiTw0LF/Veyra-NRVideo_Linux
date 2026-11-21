// veyamux-probe-test res <gray8 file> <w> <h> [frames]   - resolution estimate per frame of a raw 8-bit luma file
// veyamux-probe-test dedup                               - repeat detection: full picture vs. the sparse grid
// veyamux-probe-test fps <gray8 file> <w> <h> <frames> <signalFps> - content frame rate of a raw luma sequence
#include "ContentProbe.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
using namespace veyamux;
static int dedupTest() {
    // a 4K "game" picture, repeated with card noise (18 % of samples +-1, a few hundred +-2, one +-4), and new pictures
    // that differ only by a small moving element seen from a still camera (an idle animation: 12x12 px moving 2 px)
    const int w = 3840, h = 2160;
    std::vector<uint8_t> base(size_t(w) * h), frame(base.size());
    uint32_t s = 1;
    auto rnd = [&] { s = s * 1664525u + 1013904223u; return s >> 8; };
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) base[size_t(y) * w + x] = uint8_t(60 + (x * 7 + y * 3) % 90 + rnd() % 20);
    auto make = [&](int moverX, bool noise) {
        frame = base;
        for (int y = 1000; y < 1012; ++y) for (int x = moverX; x < moverX + 12; ++x) frame[size_t(y) * w + x] = 230;
        if (noise) {
            for (size_t i = 0; i < frame.size(); ++i) {
                const uint32_t r = rnd() % 1000;
                if (r < 90) frame[i] = uint8_t(std::min(255, frame[i] + 1));
                else if (r < 180) frame[i] = uint8_t(std::max(0, frame[i] - 1));
            }
            for (int k = 0; k < 300; ++k) { size_t i = rnd() % frame.size(); frame[i] = uint8_t(std::min(255, frame[i] + 2)); }
            size_t i = rnd() % frame.size(); frame[i] = uint8_t(std::min(255, frame[i] + 4));
        }
    };
    PictureCompare full; FrameDiff grid;
    int fullOk = 0, gridOk = 0, n = 0;
    double ms = 0;
    for (int i = 0; i < 120; ++i) {
        const bool isNew = i % 2 == 0;          // 30 fps game in 60 Hz: new, repeat, new, repeat ...
        make(1800 + (i / 2) * 2, i > 0);       // the mover steps 2 px per game frame; every sample has card noise
        const auto t0 = std::chrono::steady_clock::now();
        const bool f = full.changed(frame.data(), w, h, w, 1);
        ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const bool g = grid.changed(frame.data(), w, h, w, 1, true);
        if (i == 0) continue;
        ++n; fullOk += f == isNew; gridOk += g == isNew;
    }
    std::printf("dedup 4K, 30 fps game in 60 Hz with card noise, only a 12 px element moving:\n"
                "  full picture: %d/%d samples right (%.2f ms per sample)\n  sparse grid:  %d/%d samples right\n",
                fullOk, n, ms / 120, gridOk, n);
    return fullOk == n ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "dedup") == 0) return dedupTest();
    if (argc < 5) { std::fprintf(stderr, "usage: see source\n"); return 2; }
    const int w = atoi(argv[3]), h = atoi(argv[4]);
    const int frames = argc > 5 ? atoi(argv[5]) : 1;
    FILE* f = std::fopen(argv[2], "rb");
    if (!f) return 1;
    std::vector<uint8_t> img(size_t(w) * h);
    if (std::strcmp(argv[1], "res") == 0) {
        for (int i = 0; i < frames && std::fread(img.data(), 1, img.size(), f) == img.size(); ++i) {
            int n = 1; while (n * 2 <= std::min(w, 2048)) n *= 2;
            const int count = 48;
            std::vector<float> rows(size_t(n) * count);
            for (int r = 0; r < count; ++r) {
                const int y = h / 5 + (h * 3 / 5) * r / count;
                for (int x = 0; x < n; ++x) rows[size_t(r) * n + x] = img[size_t(y) * w + (w - n) / 2 + x];
            }
            std::printf("frame %d: estimated width %d (signal %d)\n", i, ContentProbe::estimateWidth(rows, n, count, w), w);
        }
        return 0;
    }
    const double sig = atof(argv[6]);
    ContentProbe p;
    auto t = std::chrono::steady_clock::now();
    for (int i = 0; i < frames && std::fread(img.data(), 1, img.size(), f) == img.size(); ++i) {
        p.feed(img.data(), w, h, w, 1, t);
        t += std::chrono::microseconds(int64_t(1e6 / sig));
        if (i % 30 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(30));   // let the worker run
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const auto r = p.result();
    std::printf("signal %.2f fps, content %.2f fps%s, estimated %dx%d\n", r.signalFps, r.contentFps, r.staticPicture ? " (static)" : "", r.estW, r.estH);
    return 0;
}
