// Offline check of the capture converters: capture_test <fourcc yuyv|uyvy|nv12|i420|mjpeg> <w> <h> <raw file> <out.ppm>
#include "CaptureSource.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <iterator>
#include <linux/videodev2.h>
using namespace veyamux;
int main(int argc, char** argv) {
    if (argc < 6) return 2;
    std::string f = argv[1]; int w = atoi(argv[2]), h = atoi(argv[3]);
    uint32_t cc = f == "yuyv" ? V4L2_PIX_FMT_YUYV : f == "uyvy" ? V4L2_PIX_FMT_UYVY : f == "nv12" ? V4L2_PIX_FMT_NV12 : f == "i420" ? V4L2_PIX_FMT_YUV420 : V4L2_PIX_FMT_MJPEG;
    int stride = (cc == V4L2_PIX_FMT_YUYV || cc == V4L2_PIX_FMT_UYVY) ? w * 2 : w;
    std::ifstream in(argv[4], std::ios::binary); std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    FrameConverter c; std::string err;
    if (!c.init(cc, w, h, stride, err)) { std::fprintf(stderr, "init: %s\n", err.c_str()); return 1; }
    std::vector<uint8_t> rgba;
    if (!c.convert(data.data(), data.size(), rgba, err)) { std::fprintf(stderr, "convert: %s\n", err.c_str()); return 1; }
    FILE* o = std::fopen(argv[5], "wb"); std::fprintf(o, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; ++i) std::fwrite(&rgba[i * 4], 1, 3, o);
    std::fclose(o);
    auto devs = listCaptureDevices(); std::printf("devices found: %zu (ok)\n", devs.size());
    return 0;
}
