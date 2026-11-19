// Remote Play decoder check without a console: veyamux-ps-decode-test <file.h264|file.hevc>
// (VEYAMUX_PS_SWDECODE=1 forces the software decoder)
#include "RemotePlay.h"
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: %s <file.h264|file.hevc>\n", argv[0]); return 2; }
    const std::string f = argv[1];
    const bool hevc = f.size() > 5 && (f.substr(f.size() - 5) == ".hevc" || f.substr(f.size() - 5) == ".h265");
    const auto t = veyamux::remoteplay::decodeTest(f, hevc);
    if (!t.error.empty()) { std::fprintf(stderr, "error: %s\n", t.error.c_str()); return 1; }
    std::printf("%d frames %dx%d %s, %s, %.2f ms per frame, centre luma of frame 1: %d\n", t.frames, t.w, t.h,
                t.p010 ? "P010" : "NV12", t.hw ? "VA-API" : "software", t.frames ? t.ms / t.frames : 0.0, t.firstLuma);
    return t.frames > 0 ? 0 : 1;
}
