// Abyssal Garden demo: builds the scene without VeyAmux and renders frames to PPM files (development test).
//   veyamux-abyssal-test <out-dir> [width height]      ABYSSAL_DUMP=<dir> also writes the generated world
#include "AbyssalAudio.h"
#include "AbyssalScene.h"

#include <chrono>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : ".";
    const int w = argc > 3 ? std::atoi(argv[2]) : 1920, h = argc > 3 ? std::atoi(argv[3]) : 1080;
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    abyssal::Scene scene;
    std::string err;
    if (!scene.init(w, h, [&](const char* what, double p) {
            std::printf("%6.2f s  %3.0f%%  %s\n", std::chrono::duration<double>(clk::now() - t0).count(), p * 100, what);
            std::fflush(stdout);
        }, err)) {
        std::fprintf(stderr, "init failed: %s\n", err.c_str());
        return 1;
    }
    std::printf("ready after %.2f s on %s\n", std::chrono::duration<double>(clk::now() - t0).count(), scene.adapterName().c_str());
    std::vector<uint8_t> rgba;
    for (int pose = 0; pose < 6; pose++) {
        abyssal::Controls c;
        c.pose = pose;
        scene.frame(1.0 / 60, c, rgba);
        c.pose = -1;
        const auto f0 = clk::now();
        for (int i = 0; i < 3; i++) scene.frame(1.0 / 60, c, rgba);
        std::printf("pose %d: %.1f ms per frame\n", pose + 1, std::chrono::duration<double, std::milli>(clk::now() - f0).count() / 3);
        FILE* f = std::fopen((out + "/pose" + std::to_string(pose + 1) + ".ppm").c_str(), "wb");
        std::fprintf(f, "P6\n%d %d\n255\n", scene.width(), scene.height());
        for (size_t i = 0; i < rgba.size(); i += 4) std::fwrite(&rgba[i], 1, 3, f);
        std::fclose(f);
    }
    // one second of sound
    abyssal::Synth synth;
    std::vector<int16_t> pcm(48000 * 2 * 4);
    for (int i = 0; i < 4 * 48000; i += 240) { synth.setCamera(0, -40, i * 0.0001); synth.render(pcm.data() + i * 2, 240); }
    FILE* f = std::fopen((out + "/sound.raw").c_str(), "wb");
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
    return 0;
}
