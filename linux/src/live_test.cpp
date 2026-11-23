// Live-path check: veyamux-live-test <shaderDir>
// Feeds NV12 frames through EnhanceChain::recordLive while the window size changes, as a capture card does when the
// window is resized, plus a padded stride and a short (incomplete) frame. Fails when a frame is not drawn, when a
// window resize throws away the temporal history, or when a short frame is accepted.
#include "EnhanceChain.h"
#include <cstdio>
#include <vector>
using namespace veyamux;
int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <shaderDir>\n", argv[0]); return 2; }
    VkContext ctx; std::string err;
    if (!ctx.init(err)) { std::fprintf(stderr, "vk: %s\n", err.c_str()); return 1; }
    std::printf("GPU: %s\n", ctx.deviceName().c_str());
    EnhanceChain chain(ctx, argv[1]);
    if (!chain.init(err)) { std::fprintf(stderr, "init: %s\n", err.c_str()); return 1; }
    const int W = 640, H = 360;
    EnhanceSettings s;
    s.denoise = Denoise::Temporal;   // a temporal pass, to see that resizing keeps its history
    int failures = 0;
    auto frame = [&](int stride, size_t bytes, int fitW, int fitH, bool expectOk) {
        std::vector<uint8_t> nv12(bytes, 128);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) nv12[size_t(y) * stride + x] = uint8_t((x + y) & 255);
        LiveInput in;
        in.fmt = LiveInput::NV12; in.data = nv12.data(); in.size = bytes; in.w = W; in.h = H; in.stride = stride;
        s.fitW = fitW; s.fitH = fitH;
        ctx.beginFrame();
        std::string e;
        const bool ok = chain.recordLive(ctx.cmd(), 0, in, s, e, 16.7f);
        if (!ctx.submitAndWait(e)) { std::fprintf(stderr, "submit: %s\n", e.c_str()); ++failures; return; }
        if (ok != expectOk) { std::fprintf(stderr, "frame %dx%d stride %d bytes %zu: %s (%s)\n", fitW, fitH, stride, bytes, ok ? "accepted" : "refused", e.c_str()); ++failures; }
    };
    const size_t tight = size_t(W) * H * 3 / 2;
    for (int i = 0; i < 4; ++i) frame(W, tight, 1280, 720, true);
    const uint64_t gen = chain.liveGeneration();
    for (int i = 0; i < 20; ++i) frame(W, tight, 1000 + i * 16, 560 + i * 10, true);   // the window is dragged bigger
    std::printf("window resizes: %d new display generations\n", int(chain.liveGeneration() - gen));
    const int padded = W + 128;   // a card with padded rows
    frame(padded, size_t(padded) * H * 3 / 2, 1280, 720, true);
    frame(W, tight - 1000, 1280, 720, false);   // short: must be refused
    frame(W, tight, 1280, 720, true);
    std::printf("%s\n", failures ? "FAILED" : "OK");
    return failures ? 1 : 0;
}
