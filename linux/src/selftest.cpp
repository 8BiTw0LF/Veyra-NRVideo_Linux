// Headless check of the Vulkan enhancement chain: veyamux-selftest <shaderDir> <out.ppm> [scale] [fxaa] [sharpen]
#include "EnhanceChain.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace veyamux;
int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s shaderDir out.ppm [scale] [fxaa 0/1] [sharpen]\n", argv[0]); return 2; }
    VkContext ctx; std::string err;
    if (!ctx.init(err)) { std::fprintf(stderr, "Vulkan init failed: %s\n", err.c_str()); return 1; }
    std::printf("GPU: %s%s\n", ctx.deviceName().c_str(), ctx.isSoftware() ? " (software)" : "");
    EnhanceChain chain(ctx, argv[1]);
    if (!chain.init(err)) { std::fprintf(stderr, "chain init failed: %s\n", err.c_str()); return 1; }

    const int W = 320, H = 180;
    std::vector<uint8_t> img(W * H * 4);
    for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
        uint8_t* p = &img[(y * W + x) * 4];
        p[0] = uint8_t(x * 255 / W); p[1] = uint8_t(y * 255 / H); p[2] = 128; p[3] = 255;
        // aliased diagonal white line + a hard-edged red square for AA/sharpen to act on
        if (std::abs(x - y * 2) < 2) p[0] = p[1] = p[2] = 255;
        if (x > 200 && x < 280 && y > 40 && y < 120) { p[0] = 220; p[1] = 30; p[2] = 30; }
    }
    EnhanceSettings s;
    if (argc > 3) s.scale = float(std::atof(argv[3]));
    if (argc > 4) s.fxaa = std::atoi(argv[4]) != 0;
    if (argc > 5) s.sharpen = float(std::atof(argv[5]));
    std::vector<uint8_t> out; int ow = 0, oh = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i)
        if (!chain.process(img.data(), W, H, W * 4, s, out, ow, oh, err)) { std::fprintf(stderr, "process failed: %s\n", err.c_str()); return 1; }
    auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 3;
    std::printf("%dx%d -> %dx%d, %.1f ms/frame\n", W, H, ow, oh, ms);
    FILE* f = std::fopen(argv[2], "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", ow, oh);
    for (int i = 0; i < ow * oh; ++i) std::fwrite(&out[i * 4], 1, 3, f);
    std::fclose(f);
    return 0;
}
