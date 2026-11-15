// veyamux-probe-test res <gray8 file> <w> <h> [frames]   - resolution estimate per frame of a raw 8-bit luma file
// veyamux-probe-test fps <gray8 file> <w> <h> <frames> <signalFps> - content frame rate of a raw luma sequence
#include "ContentProbe.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>
using namespace veyamux;
int main(int argc, char** argv) {
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
