#include "CaptureSource.h"
#include "DemoSource.h"
#include "RemotePlay.h"
#include "ScreenCast.h"
#include <cstdio>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <immintrin.h>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <climits>
#include <string>
#include <dirent.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef V4L2_PIX_FMT_P010
#define V4L2_PIX_FMT_P010 v4l2_fourcc('P', '0', '1', '0')   // 10-bit 4:2:0, Y plane + interleaved CbCr, 16 bits per sample
#endif

namespace veyamux {
namespace {
int xioctl(int fd, unsigned long req, void* arg) {
    int r;
    do { r = ioctl(fd, req, arg); } while (r == -1 && errno == EINTR);
    return r;
}
// Which other process has the device open? ("name (PID n)", empty if unknown.)
std::string deviceOwner(const std::string& path) {
    char real[4096];
    if (!realpath(path.c_str(), real)) return {};
    const std::string target = real;
    const long self = long(getpid());
    DIR* proc = opendir("/proc");
    if (!proc) return {};
    std::string result;
    while (dirent* e = readdir(proc)) {
        char* end = nullptr;
        const long pid = std::strtol(e->d_name, &end, 10);
        if (!end || *end || pid <= 0 || pid == self) continue;
        const std::string fdDir = std::string("/proc/") + e->d_name + "/fd";
        DIR* fds = opendir(fdDir.c_str());
        if (!fds) continue;   // other users' processes are not readable
        bool found = false;
        while (dirent* f = readdir(fds)) {
            char link[4096];
            const ssize_t n = readlink((fdDir + "/" + f->d_name).c_str(), link, sizeof(link) - 1);
            if (n > 0) { link[n] = 0; if (target == link) { found = true; break; } }
        }
        closedir(fds);
        if (!found) continue;
        std::string name;
        if (FILE* c = std::fopen((std::string("/proc/") + e->d_name + "/comm").c_str(), "r")) {
            char buf[64] = {};
            if (std::fgets(buf, sizeof(buf), c)) name = buf;
            std::fclose(c);
            while (!name.empty() && (name.back() == '\n' || name.back() == ' ')) name.pop_back();
        }
        result = (name.empty() ? std::string("?") : name) + " (PID " + std::to_string(pid) + ")";
        break;
    }
    closedir(proc);
    return result;
}
AVPixelFormat pixFmtFor(uint32_t f) {
    switch (f) {
    case V4L2_PIX_FMT_YUYV: return AV_PIX_FMT_YUYV422;
    case V4L2_PIX_FMT_UYVY: return AV_PIX_FMT_UYVY422;
    case V4L2_PIX_FMT_NV12: return AV_PIX_FMT_NV12;
    case V4L2_PIX_FMT_P010: return AV_PIX_FMT_P010LE;
    case V4L2_PIX_FMT_YUV420: return AV_PIX_FMT_YUV420P;
    case V4L2_PIX_FMT_RGB24: return AV_PIX_FMT_RGB24;
    case V4L2_PIX_FMT_BGR24: return AV_PIX_FMT_BGR24;
    default: return AV_PIX_FMT_NONE;
    }
}
bool isMjpeg(uint32_t f) { return f == V4L2_PIX_FMT_MJPEG || f == V4L2_PIX_FMT_JPEG; }
bool supported(uint32_t f) { return isMjpeg(f) || pixFmtFor(f) != AV_PIX_FMT_NONE; }
bool compressed(uint32_t f) { return isMjpeg(f); }
} // namespace

std::string fourccName(uint32_t f) {
    switch (f) {
    case V4L2_PIX_FMT_YUYV: return "YUYV";
    case V4L2_PIX_FMT_UYVY: return "UYVY";
    case V4L2_PIX_FMT_NV12: return "NV12";
    case V4L2_PIX_FMT_P010: return "P010 (HDR)";
    case V4L2_PIX_FMT_YUV420: return "I420";
    case V4L2_PIX_FMT_RGB24: return "RGB24";
    case V4L2_PIX_FMT_BGR24: return "BGR24";
    case V4L2_PIX_FMT_MJPEG: case V4L2_PIX_FMT_JPEG: return "MJPEG";
    default: { char s[5] = {char(f), char(f >> 8), char(f >> 16), char(f >> 24), 0}; return s; }
    }
}

std::vector<CaptureDevice> listCaptureDevices() {
    std::vector<CaptureDevice> out;
    DIR* d = opendir("/dev");
    if (!d) return out;
    std::vector<std::string> names;
    while (dirent* e = readdir(d))
        if (std::strncmp(e->d_name, "video", 5) == 0) names.push_back(e->d_name);
    closedir(d);
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) {
        return (a.size() != b.size()) ? a.size() < b.size() : a < b;   // video2 < video10
    });
    for (const auto& n : names) {
        const std::string path = "/dev/" + n;
        int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd < 0) continue;
        v4l2_capability cap{};
        if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            const uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
            if ((caps & V4L2_CAP_VIDEO_CAPTURE) && (caps & V4L2_CAP_STREAMING))
                out.push_back({path, reinterpret_cast<const char*>(cap.card)});
        }
        ::close(fd);
    }
    return out;
}

std::vector<CaptureMode> listCaptureModes(const std::string& path) {
    std::vector<CaptureMode> modes;
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) return modes;
    static const int kSizes[][2] = {{3840, 2160}, {2560, 1440}, {1920, 1200}, {1920, 1080}, {1600, 900},
                                    {1280, 720}, {1024, 768}, {800, 600}, {720, 576}, {720, 480}, {640, 480}};
    static const int kFps[] = {120, 60, 50, 30, 25, 24};
    for (uint32_t i = 0;; ++i) {
        v4l2_fmtdesc fd_{};
        fd_.index = i;
        fd_.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (xioctl(fd, VIDIOC_ENUM_FMT, &fd_) != 0) break;
        if (!supported(fd_.pixelformat)) continue;
        std::vector<std::pair<int, int>> sizes;
        for (uint32_t j = 0;; ++j) {
            v4l2_frmsizeenum fs{};
            fs.index = j; fs.pixel_format = fd_.pixelformat;
            if (xioctl(fd, VIDIOC_ENUM_FRAMESIZES, &fs) != 0) break;
            if (fs.type == V4L2_FRMSIZE_TYPE_DISCRETE) sizes.push_back({int(fs.discrete.width), int(fs.discrete.height)});
            else {
                for (auto& s : kSizes)
                    if (uint32_t(s[0]) >= fs.stepwise.min_width && uint32_t(s[0]) <= fs.stepwise.max_width &&
                        uint32_t(s[1]) >= fs.stepwise.min_height && uint32_t(s[1]) <= fs.stepwise.max_height)
                        sizes.push_back({s[0], s[1]});
                break;
            }
        }
        for (auto [w, h] : sizes) {
            std::vector<std::pair<uint32_t, uint32_t>> ivals;   // num, den
            for (uint32_t k = 0;; ++k) {
                v4l2_frmivalenum fi{};
                fi.index = k; fi.pixel_format = fd_.pixelformat; fi.width = uint32_t(w); fi.height = uint32_t(h);
                if (xioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &fi) != 0) break;
                if (fi.type == V4L2_FRMIVAL_TYPE_DISCRETE) ivals.push_back({fi.discrete.numerator, fi.discrete.denominator});
                else {
                    for (int f : kFps) {
                        const double iv = 1.0 / f;
                        if (iv >= double(fi.stepwise.min.numerator) / fi.stepwise.min.denominator - 1e-9 &&
                            iv <= double(fi.stepwise.max.numerator) / fi.stepwise.max.denominator + 1e-9)
                            ivals.push_back({1, uint32_t(f)});
                    }
                    break;
                }
            }
            if (ivals.empty()) ivals.push_back({1, 30});
            for (auto [n, dn] : ivals) {
                if (n == 0 || dn == 0) continue;
                CaptureMode m;
                m.fourcc = fd_.pixelformat; m.w = w; m.h = h; m.num = n; m.den = dn;
                m.fps = double(dn) / n;
                char buf[96];
                std::snprintf(buf, sizeof buf, "%dx%d @ %.3g fps · %s", w, h, std::round(m.fps * 100) / 100, fourccName(m.fourcc).c_str());
                m.label = buf;
                modes.push_back(m);
            }
        }
    }
    ::close(fd);
    orderCaptureModes(modes);
    return modes;
}

void orderCaptureModes(std::vector<CaptureMode>& modes) {
    std::stable_sort(modes.begin(), modes.end(), [](const CaptureMode& a, const CaptureMode& b) {
        const bool ah = a.fps >= 50, bh = b.fps >= 50;
        if (ah != bh) return ah;
        const long pa = long(a.w) * a.h, pb = long(b.w) * b.h;
        if (pa != pb) return pa > pb;
        if (std::abs(a.fps - b.fps) > 0.5) return a.fps > b.fps;
        // HDR (P010) only when chosen on purpose: twice the USB bandwidth, and an SDR source needs no tone mapping
        const bool ap = a.fourcc == V4L2_PIX_FMT_P010, bp = b.fourcc == V4L2_PIX_FMT_P010;
        if (ap != bp) return bp;
        return !compressed(a.fourcc) && compressed(b.fourcc);
    });
    // First (the default, marked ★): the largest picture the card sends uncompressed, at its highest rate (on a
    // 4K card typically 4K NV12 at 30 fps: sharpest, no decoding, least delay). Right below it: the same size at a
    // higher rate, compressed if need be (4K MJPEG at 60 fps), for 60 fps games.
    auto pixels = [](const CaptureMode& m) { return long(m.w) * m.h; };
    auto raw = [](const CaptureMode& m) { return !compressed(m.fourcc) && m.fourcc != V4L2_PIX_FMT_P010; };
    int first = -1;
    for (int i = 0; i < int(modes.size()); ++i) {
        if (!raw(modes[size_t(i)])) continue;
        const auto& m = modes[size_t(i)];
        if (first < 0 || pixels(m) > pixels(modes[size_t(first)]) ||
            (pixels(m) == pixels(modes[size_t(first)]) && m.fps > modes[size_t(first)].fps + 0.5) ||
            // same size and rate: NV12 (12 bits a pixel) before YUYV/UYVY (16): less USB and less to upload
            (pixels(m) == pixels(modes[size_t(first)]) && std::abs(m.fps - modes[size_t(first)].fps) <= 0.5 &&
             m.fourcc == V4L2_PIX_FMT_NV12 && modes[size_t(first)].fourcc != V4L2_PIX_FMT_NV12)) first = i;
    }
    if (first >= 0) {
        const CaptureMode top = modes[size_t(first)];
        modes.erase(modes.begin() + first);
        int second = -1;
        for (int i = 0; i < int(modes.size()); ++i) {
            const auto& m = modes[size_t(i)];
            if (m.w != top.w || m.h != top.h || m.fps <= top.fps + 0.5 || m.fourcc == V4L2_PIX_FMT_P010) continue;
            if (second < 0 || m.fps > modes[size_t(second)].fps + 0.5) second = i;   // the sort put the best format first
        }
        if (second >= 0) {
            const CaptureMode next = modes[size_t(second)];
            modes.erase(modes.begin() + second);
            modes.insert(modes.begin(), next);
        }
        // between them: 1080p uncompressed at the same rate (a quarter of the USB traffic, so much less delay; VeyAmux
        // can enlarge it again)
        if (top.h > 1080) {
            int mid = -1;
            for (int i = 0; i < int(modes.size()); ++i) {
                const auto& m = modes[size_t(i)];
                if (m.h != 1080 || !raw(m)) continue;
                const auto score = [&](const CaptureMode& x) {   // same rate as the top first, then NV12
                    return (std::abs(x.fps - top.fps) <= 0.5 ? 2 : 0) + (x.fourcc == V4L2_PIX_FMT_NV12 ? 1 : 0);
                };
                if (mid < 0 || score(m) > score(modes[size_t(mid)])) mid = i;
            }
            if (mid >= 0) {
                const CaptureMode m = modes[size_t(mid)];
                modes.erase(modes.begin() + mid);
                modes.insert(modes.begin(), m);   // ahead of the faster variant
            }
        }
        modes.insert(modes.begin(), top);
    }
}

// ------------------------------------------------------------------ FrameConverter

FrameConverter::FrameConverter() = default;
FrameConverter::~FrameConverter() {
    sws_freeContext(sws_);
    if (mjpeg_) avcodec_free_context(&mjpeg_);
    if (hwDec_) avcodec_free_context(&hwDec_);
    av_buffer_unref(&hwDevice_);
    av_frame_free(&hwFrame_);
    av_frame_free(&swFrame_);
    av_frame_free(&frame_);
    av_packet_free(&pkt_);
}

bool FrameConverter::init(uint32_t fourcc, int w, int h, int stride, std::string& error) {
    fourcc_ = fourcc; w_ = w; h_ = h; stride_ = stride;
    if (isMjpeg(fourcc)) {
        const AVCodec* c = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
        if (!c) { error = "FFmpeg has no MJPEG decoder"; return false; }
        mjpeg_ = avcodec_alloc_context3(c);
        mjpeg_->thread_count = 2;
        mjpeg_->thread_type = FF_THREAD_SLICE;     // frame threading would add latency
        mjpeg_->flags |= AV_CODEC_FLAG_LOW_DELAY;
        if (avcodec_open2(mjpeg_, c, nullptr) < 0) { error = "Could not open the MJPEG decoder"; return false; }
        frame_ = av_frame_alloc();
        pkt_ = av_packet_alloc();
    } else if (pixFmtFor(fourcc) == AV_PIX_FMT_NONE) {
        error = "Unsupported pixel format " + fourccName(fourcc);
        return false;
    }
    return true;
}

bool FrameConverter::convert(const uint8_t* data, size_t size, std::vector<uint8_t>& rgba, std::string& error) {
    const uint8_t* planes[4] = {nullptr, nullptr, nullptr, nullptr};
    int lines[4] = {0, 0, 0, 0};
    AVPixelFormat fmt;
    int range = fullRange_ ? 1 : 0;
    int w = w_, h = h_;
    if (mjpeg_) {
        // FFmpeg may read up to AV_INPUT_BUFFER_PADDING_SIZE bytes past a packet; the driver's buffer has no such
        // padding, so the JPEG is copied into one that does
        if (size == 0 || size > size_t(INT32_MAX) - AV_INPUT_BUFFER_PADDING_SIZE) { error = "Corrupt MJPEG frame"; return false; }
        packet_.resize(size + AV_INPUT_BUFFER_PADDING_SIZE);
        std::memcpy(packet_.data(), data, size);
        std::memset(packet_.data() + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
        pkt_->data = packet_.data();
        pkt_->size = int(size);
        if (avcodec_send_packet(mjpeg_, pkt_) < 0 || avcodec_receive_frame(mjpeg_, frame_) < 0) {
            ++corrupt_;
            error = "Corrupt MJPEG frame";
            return false;
        }
        w = frame_->width; h = frame_->height; fmt = AVPixelFormat(frame_->format);
        for (int i = 0; i < 4; ++i) { planes[i] = frame_->data[i]; lines[i] = frame_->linesize[i]; }
        range = (frame_->color_range == AVCOL_RANGE_MPEG) ? 0 : 1;   // JPEG is full range unless flagged
    } else {
        fmt = pixFmtFor(fourcc_);
        const int bpl = stride_;
        // every plane must lie inside what the card delivered
        size_t need = size_t(bpl) * h;
        if (fourcc_ == V4L2_PIX_FMT_NV12 || fourcc_ == V4L2_PIX_FMT_P010 || fourcc_ == V4L2_PIX_FMT_YUV420) need = need * 3 / 2;
        if (bpl <= 0 || w <= 0 || h <= 0 || size < need) { error = "Incomplete frame from the capture card"; return false; }
        switch (fourcc_) {
        case V4L2_PIX_FMT_NV12:
        case V4L2_PIX_FMT_P010:
            planes[0] = data; lines[0] = bpl; planes[1] = data + size_t(bpl) * h; lines[1] = bpl; break;
        case V4L2_PIX_FMT_YUV420:
            planes[0] = data; lines[0] = bpl;
            planes[1] = data + size_t(bpl) * h; lines[1] = bpl / 2;
            planes[2] = planes[1] + size_t(bpl / 2) * (h / 2); lines[2] = bpl / 2; break;
        default: planes[0] = data; lines[0] = bpl; break;
        }
        if (fmt == AV_PIX_FMT_RGB24 || fmt == AV_PIX_FMT_BGR24) range = 1;
    }
    sws_ = sws_getCachedContext(sws_, w, h, fmt, w, h, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) { error = "sws_getCachedContext failed"; return false; }
    const int cs = (h >= 720 || w >= 1280) ? SWS_CS_ITU709 : SWS_CS_ITU601;
    sws_setColorspaceDetails(sws_, sws_getCoefficients(cs), range, sws_getCoefficients(SWS_CS_DEFAULT), 1, 0, 1 << 16, 1 << 16);
    rgba.resize(size_t(w) * h * 4);
    uint8_t* dst[4] = {rgba.data(), nullptr, nullptr, nullptr};
    int dstLines[4] = {w * 4, 0, 0, 0};
    sws_scale(sws_, planes, lines, 0, h, dst, dstLines);
    w_ = w; h_ = h;
    return true;
}

namespace {
AVPixelFormat gHwFmt = AV_PIX_FMT_NONE;   // the format the hardware decoder being opened hands out
AVPixelFormat pickHw(AVCodecContext*, const AVPixelFormat* fmts) {
    for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == gHwFmt) return *p;
    return AV_PIX_FMT_NONE;   // no hardware path for this picture: the decode fails and the CPU takes over
}
// Copies rows out of memory the CPU reads slowly: a picture the graphics card maps for reading sits in write-combined
// (uncached) memory, where ordinary loads crawl (~1-2 GB/s, 8+ ms for one 4K picture). SSE4.1 streaming loads
// (MOVNTDQA) read it at full speed.
__attribute__((target("sse4.1"))) void streamCopySse41(uint8_t* dst, const uint8_t* src, size_t n) {
    size_t i = 0;
    for (; i + 64 <= n; i += 64) {
        const __m128i a = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(src + i)));
        const __m128i b = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(src + i + 16)));
        const __m128i c = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(src + i + 32)));
        const __m128i d = _mm_stream_load_si128(reinterpret_cast<__m128i*>(const_cast<uint8_t*>(src + i + 48)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), a);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 16), b);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 32), c);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 48), d);
    }
    if (i < n) std::memcpy(dst + i, src + i, n - i);
}
void copyRow(uint8_t* dst, const uint8_t* src, size_t n, bool uncached) {
    static const bool sse41 = __builtin_cpu_supports("sse4.1");
    if (uncached && sse41 && (reinterpret_cast<uintptr_t>(src) & 15) == 0) streamCopySse41(dst, src, n);
    else std::memcpy(dst, src, n);
}
// NV12 from a decoded picture: NV12 as is, 4:2:0 / 4:2:2 planar (JPEG's own formats) with the chroma interleaved
// (4:2:2 averaged down to 4:2:0). False for anything else.
bool toNv12(const AVFrame* f, std::vector<uint8_t>& out, int& stride, bool uncached = false) {
    const int w = f->width & ~1, h = f->height & ~1;
    if (w <= 0 || h <= 0) return false;
    const auto fmt = AVPixelFormat(f->format);
    const bool nv12 = fmt == AV_PIX_FMT_NV12;
    const bool p420 = fmt == AV_PIX_FMT_YUVJ420P || fmt == AV_PIX_FMT_YUV420P;
    const bool p422 = fmt == AV_PIX_FMT_YUVJ422P || fmt == AV_PIX_FMT_YUV422P;
    if (!nv12 && !p420 && !p422) return false;
    stride = w;
    out.resize(size_t(w) * h * 3 / 2);
    for (int y = 0; y < h; ++y) copyRow(out.data() + size_t(y) * w, f->data[0] + size_t(y) * f->linesize[0], size_t(w), uncached);
    uint8_t* uv = out.data() + size_t(w) * h;
    const int cw = w / 2;
    for (int y = 0; y < h / 2; ++y) {
        uint8_t* d = uv + size_t(y) * w;
        if (nv12) { copyRow(d, f->data[1] + size_t(y) * f->linesize[1], size_t(w), uncached); continue; }
        const int r0 = p422 ? 2 * y : y, r1 = p422 ? 2 * y + 1 : y;
        const uint8_t* u0 = f->data[1] + size_t(r0) * f->linesize[1];
        const uint8_t* v0 = f->data[2] + size_t(r0) * f->linesize[2];
        const uint8_t* u1 = f->data[1] + size_t(r1) * f->linesize[1];
        const uint8_t* v1 = f->data[2] + size_t(r1) * f->linesize[2];
        for (int x = 0; x < cw; ++x) {
            d[2 * x] = uint8_t((u0[x] + u1[x] + 1) >> 1);
            d[2 * x + 1] = uint8_t((v0[x] + v1[x] + 1) >> 1);
        }
    }
    return true;
}
} // namespace

bool FrameConverter::openHw() {
    hwTried_ = true;
    const AVCodec* c = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
    if (!c) return false;
    struct Try { AVHWDeviceType type; const char* name; };
    // VA-API: AMD and Intel (and the graphics card VEYAMUX_VAAPI_DEVICE names); CUDA: NVIDIA's NVDEC
    for (const Try& t : {Try{AV_HWDEVICE_TYPE_VAAPI, "VA-API"}, Try{AV_HWDEVICE_TYPE_CUDA, "NVDEC"}}) {
        AVPixelFormat fmt = AV_PIX_FMT_NONE;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* cfg = avcodec_get_hw_config(c, i);
            if (!cfg) break;
            if (cfg->device_type == t.type && (cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) { fmt = cfg->pix_fmt; break; }
        }
        if (fmt == AV_PIX_FMT_NONE) continue;   // this FFmpeg has no such path for MJPEG
        const char* dev = t.type == AV_HWDEVICE_TYPE_VAAPI ? std::getenv("VEYAMUX_VAAPI_DEVICE") : nullptr;
        AVBufferRef* device = nullptr;
        if (av_hwdevice_ctx_create(&device, t.type, dev, nullptr, 0) < 0) continue;
        AVCodecContext* ctx = avcodec_alloc_context3(c);
        ctx->hw_device_ctx = av_buffer_ref(device);
        ctx->get_format = pickHw;
        ctx->flags |= AV_CODEC_FLAG_LOW_DELAY;
        gHwFmt = fmt;
        if (avcodec_open2(ctx, c, nullptr) < 0) { avcodec_free_context(&ctx); av_buffer_unref(&device); continue; }
        hwDec_ = ctx;
        hwDevice_ = device;
        hwFrame_ = av_frame_alloc();
        swFrame_ = av_frame_alloc();
        path_ = t.type == AV_HWDEVICE_TYPE_VAAPI ? 1 : 2;
        std::fprintf(stderr, "MJPEG: decoded on the graphics card (%s)\n", t.name);
        return true;
    }
    return false;
}

bool FrameConverter::decodeNv12(const uint8_t* data, size_t size, std::vector<uint8_t>& nv12, int& w, int& h, int& stride,
                                bool& fullRange, bool* unsupported, std::string& error) {
    if (unsupported) *unsupported = false;
    if (!mjpeg_) { if (unsupported) *unsupported = true; return false; }
    const auto t0 = std::chrono::steady_clock::now();
    if (size == 0 || size > size_t(INT32_MAX) - AV_INPUT_BUFFER_PADDING_SIZE) { ++corrupt_; error = "Corrupt MJPEG frame"; return false; }
    packet_.resize(size + AV_INPUT_BUFFER_PADDING_SIZE);
    std::memcpy(packet_.data(), data, size);
    std::memset(packet_.data() + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    pkt_->data = packet_.data();
    pkt_->size = int(size);
    if (mjpegMode_ != kMjpegCpu && !hwTried_) openHw();
    bool done = false;
    if (hwDec_ && !hwFailed_ && mjpegMode_ != kMjpegCpu) {
        // the graphics card: decode, then fetch the picture as NV12
        const bool decoded = avcodec_send_packet(hwDec_, pkt_) >= 0 && avcodec_receive_frame(hwDec_, hwFrame_) >= 0;
        const auto t1 = std::chrono::steady_clock::now();
        av_frame_unref(swFrame_);
        if (decoded && hwFrame_->format == gHwFmt) {
            // Read the picture where it lies (mapped, one copy with streaming loads) rather than have FFmpeg copy it
            // into a frame of its own first (two copies, the first with slow loads from uncached memory). NVDEC and
            // drivers that cannot map fall back to the transfer.
            bool mapped = false;
            if (!noMap_) {
                swFrame_->format = AV_PIX_FMT_NONE;
                mapped = av_hwframe_map(swFrame_, hwFrame_, AV_HWFRAME_MAP_READ) == 0;
                if (!mapped) { av_frame_unref(swFrame_); noMap_ = true; }
            }
            if (mapped || av_hwframe_transfer_data(swFrame_, hwFrame_, 0) == 0) {
                swFrame_->width = hwFrame_->width; swFrame_->height = hwFrame_->height;
                done = toNv12(swFrame_, nv12, stride, mapped);
                fullRange = hwFrame_->color_range != AVCOL_RANGE_MPEG;   // JPEG is full range unless flagged
                w = swFrame_->width & ~1; h = swFrame_->height & ~1;
            }
            av_frame_unref(swFrame_);
            if (done) {
                const double c = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
                const double old = copyMs_.load();
                copyMs_ = old <= 0 ? c : old + 0.1 * (c - old);
                mapped_ = mapped;
            }
        }
        av_frame_unref(hwFrame_);
        // A picture the graphics card cannot decode or hand over goes to the CPU. If the CPU manages what the card
        // did not three times running, the card cannot do this card's JPEGs: the CPU decodes from then on.
        if (done) hwMisses_ = 0;
    }
    if (!done) {
        if (avcodec_send_packet(mjpeg_, pkt_) < 0 || avcodec_receive_frame(mjpeg_, frame_) < 0) {
            ++corrupt_;   // neither could decode it: the card sent a broken picture
            error = "Corrupt MJPEG frame";
            return false;
        }
        if (hwDec_ && !hwFailed_ && mjpegMode_ != kMjpegCpu && ++hwMisses_ >= 3) {
            hwFailed_ = true;
            std::fprintf(stderr, "MJPEG: the graphics card cannot decode this card's pictures; decoding on the CPU\n");
        }
        done = toNv12(frame_, nv12, stride);
        fullRange = frame_->color_range != AVCOL_RANGE_MPEG;
        w = frame_->width & ~1; h = frame_->height & ~1;
        if (!done) { if (unsupported) *unsupported = true; return false; }
        path_ = 3;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    const double old = decodeMs_.load();
    decodeMs_ = old <= 0 ? ms : old + 0.1 * (ms - old);
    return true;
}

// ------------------------------------------------------------------ CaptureSource

CaptureSource::CaptureSource() = default;
CaptureSource::~CaptureSource() { close(); }

bool CaptureSource::open(const std::string& path, const CaptureMode& mode, std::string& error) {
    close();
    probe_.reset();
    delivered_.reset();
    rawCompare_.reset();
    newSinceDelivered_ = true;
    if (isRemotePlay(path)) {
        auto s = std::make_unique<remoteplay::Stream>();
        if (!s->open(path, error)) return false;
        w_ = s->width(); h_ = s->height(); fps_ = s->fps();
        card_ = s->name();
        fourcc_ = 0;
        ps_ = std::move(s);
        return true;
    }
    if (isDemo(path)) {
        auto d = std::make_unique<demo::Stream>();
        // "demo:<fps>[:<height>]"
        const char* colon = std::strchr(path.c_str() + 5, ':');
        if (!d->open(std::atoi(path.c_str() + 5), colon ? std::atoi(colon + 1) : 1080, error)) return false;
        demo_ = std::move(d);
        w_ = demo_->width(); h_ = demo_->height(); fps_ = demo_->fps();
        card_ = "Abyssal Garden (demo)";
        fourcc_ = 0;
        return true;
    }
    if (isScreenCast(path)) {
        int pfd = -1; unsigned node = 0;
        if (std::sscanf(path.c_str(), "pipewire:%d:%u", &pfd, &node) != 2) { error = "Bad screen share address"; return false; }
        auto sc = std::make_unique<ScreenCastStream>();
        if (!sc->open(pfd, node, error)) return false;
        sc_ = std::move(sc);
        w_ = sc_->width(); h_ = sc_->height(); fps_ = sc_->fps();
        card_ = "Window / screen";
        fourcc_ = 0;
        return true;
    }
    v4l2_format fmt{};
    // A device another process is still streaming from answers EBUSY. That is typically the previous VeyAmux still
    // shutting down after "Restart now" (the Vulkan layers can make that take a moment): wait up to 5 s for it.
    for (int attempt = 0;; ++attempt) {
        fd_ = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd_ < 0) { error = "Could not open " + path + ": " + std::strerror(errno); return false; }
        v4l2_capability cap{};
        if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) == 0) card_ = reinterpret_cast<const char*>(cap.card);
        fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.width = uint32_t(mode.w);
        fmt.fmt.pix.height = uint32_t(mode.h);
        fmt.fmt.pix.pixelformat = mode.fourcc;
        fmt.fmt.pix.field = V4L2_FIELD_ANY;
        if (xioctl(fd_, VIDIOC_S_FMT, &fmt) == 0) break;
        const int err = errno;
        ::close(fd_); fd_ = -1;
        if (err == EBUSY && attempt < 20) { usleep(250 * 1000); continue; }
        if (err == EBUSY) {
            const std::string owner = deviceOwner(path);
            error = "The capture device is in use by another program" + (owner.empty() ? std::string() : ": " + owner);
        } else {
            error = std::string("VIDIOC_S_FMT: ") + std::strerror(err);
        }
        return false;
    }
    if (fmt.fmt.pix.pixelformat != mode.fourcc) { error = "The device refused pixel format " + fourccName(mode.fourcc); close(); return false; }
    w_ = int(fmt.fmt.pix.width); h_ = int(fmt.fmt.pix.height);
    fourcc_ = mode.fourcc;
    rawStride_ = int(fmt.fmt.pix.bytesperline);
    if (rawStride_ <= 0) rawStride_ = fourcc_ == V4L2_PIX_FMT_NV12 ? w_ : w_ * 2;   // NV12 1, P010/YUYV/UYVY 2 bytes per luma
    // YUV from a card is limited range (16-235) unless the driver says otherwise
    fullRange_ = fmt.fmt.pix.quantization == V4L2_QUANTIZATION_FULL_RANGE;

    v4l2_streamparm parm{};
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe = {mode.num, mode.den};
    if (xioctl(fd_, VIDIOC_S_PARM, &parm) == 0 && parm.parm.capture.timeperframe.numerator)
        fps_ = double(parm.parm.capture.timeperframe.denominator) / parm.parm.capture.timeperframe.numerator;
    else fps_ = mode.fps;

    if (!conv_.init(mode.fourcc, w_, h_, rawStride_, error)) { close(); return false; }
    conv_.setFullRange(fullRange_);

    v4l2_requestbuffers req{};
    // 6: the renderer may hold two (the newest and the one on screen) without starving the card
    req.count = 8; req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &req) != 0 || req.count < 2) { error = "The device does not support memory-mapped streaming"; close(); return false; }
    bufs_.resize(req.count);
    for (uint32_t i = 0; i < req.count; ++i) {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; b.memory = V4L2_MEMORY_MMAP; b.index = i;
        if (xioctl(fd_, VIDIOC_QUERYBUF, &b) != 0) { error = "VIDIOC_QUERYBUF failed"; close(); return false; }
        bufs_[i].length = b.length;
        bufs_[i].start = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, b.m.offset);
        if (bufs_[i].start == MAP_FAILED) { bufs_[i].start = nullptr; error = "mmap failed"; close(); return false; }
        if (xioctl(fd_, VIDIOC_QBUF, &b) != 0) { error = "VIDIOC_QBUF failed"; close(); return false; }
    }
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) != 0) { error = std::string("VIDIOC_STREAMON: ") + std::strerror(errno); close(); return false; }
    streaming_ = true;
    map_ = std::make_shared<Mapping>();
    map_->fd = fd_;
    map_->bufs = bufs_;   // the mapping now belongs to map_ (unmapped with it)
    return true;
}

CaptureSource::Mapping::~Mapping() {
    for (auto& b : bufs) if (b.start) munmap(b.start, b.length);
}

void CaptureSource::close() {
    sc_.reset();
    demo_.reset();
    ps_.reset();
    if (fd_ < 0) return;
    if (map_) { std::lock_guard l(map_->m); map_->live = false; }   // buffers still held are not queued again
    if (streaming_) { int type = V4L2_BUF_TYPE_VIDEO_CAPTURE; xioctl(fd_, VIDIOC_STREAMOFF, &type); streaming_ = false; }
    if (map_) map_.reset();   // unmapped when the last held frame is gone
    else for (auto& b : bufs_) if (b.start) munmap(b.start, b.length);
    bufs_.clear();
    ::close(fd_);
    fd_ = -1;
    std::lock_guard l(arrM_);
    lastArrival_ = {};   // the gap across a reopen is not the card's rhythm
}

namespace {
std::chrono::steady_clock::time_point captureTime(const v4l2_buffer& b) {
    // V4L2 timestamps are CLOCK_MONOTONIC on current drivers, the same clock as steady_clock on Linux
    if ((b.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC && (b.timestamp.tv_sec || b.timestamp.tv_usec))
        return std::chrono::steady_clock::time_point(std::chrono::seconds(b.timestamp.tv_sec) + std::chrono::microseconds(b.timestamp.tv_usec));
    return std::chrono::steady_clock::now();
}
} // namespace

void CaptureSource::probeRaw(const v4l2_buffer& b) {
    if ((b.flags & V4L2_BUF_FLAG_ERROR) || b.index >= bufs_.size()) return;
    const auto* src = static_cast<const uint8_t*>(bufs_[b.index].start);
    if (!src || rawStride_ <= 0) return;
    if (b.bytesused < size_t(rawStride_) * h_) return;
    const uint8_t* luma = nullptr;
    int step = 2;
    if (fourcc_ == V4L2_PIX_FMT_NV12) { luma = src; step = 1; }
    else if (fourcc_ == V4L2_PIX_FMT_YUYV) luma = src;
    else if (fourcc_ == V4L2_PIX_FMT_UYVY || fourcc_ == V4L2_PIX_FMT_P010) luma = src + 1;   // P010: high byte of 16-bit luma
    if (!luma) return;
    // every buffer (also ones dropped for latency) is compared once, in full, against the last new picture; the result
    // feeds the measured picture rate and decides whether the next frame handed out is a repeat (readLatest)
    static const bool sparse = std::getenv("VEYAMUX_DEDUP_SPARSE") != nullptr;   // the old sparse grid, to compare
    if (sparse) { probe_.feed(luma, w_, h_, rawStride_, step, captureTime(b)); return; }
    const bool isNew = rawCompare_.changed(luma, w_, h_, rawStride_, step);
    newSinceDelivered_ = newSinceDelivered_ || isNew;
    probe_.feed(luma, w_, h_, rawStride_, step, captureTime(b), isNew ? 1 : 0);
    // compressed/RGB formats are probed after conversion (probeRgba)
}

void CaptureSource::probeRgba(const std::vector<uint8_t>& rgba) {
    if (fourcc_ == V4L2_PIX_FMT_NV12 || fourcc_ == V4L2_PIX_FMT_YUYV || fourcc_ == V4L2_PIX_FMT_UYVY || fourcc_ == V4L2_PIX_FMT_P010) return;
    if (rgba.size() >= size_t(w_) * h_ * 4) probe_.feed(rgba.data() + 1, w_, h_, w_ * 4, 4, std::chrono::steady_clock::now());   // green ~ luma
}

bool CaptureSource::dequeueNewest(v4l2_buffer& held, std::string& error) {
    bool got = false;
    for (size_t i = 0; i <= bufs_.size(); ++i) {
        v4l2_buffer b{};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd_, VIDIOC_DQBUF, &b) != 0) {
            if (errno == EAGAIN) break;
            error = std::string("VIDIOC_DQBUF: ") + std::strerror(errno);
            return false;
        }
        ++delivered_n_;                                     // every picture the card sent (also ones skipped for latency)
        if (b.flags & V4L2_BUF_FLAG_ERROR) ++badBuffers_;   // the driver marked it broken (USB trouble)
        {
            const auto now = std::chrono::steady_clock::now();
            std::lock_guard l(arrM_);
            if (lastArrival_.time_since_epoch().count() && arrivals_.size() < 2400)
                arrivals_.push_back(std::chrono::duration<double, std::milli>(now - lastArrival_).count());
            lastArrival_ = now;
        }
        probeRaw(b);                                    // every delivered frame, including ones dropped for latency
        if (got) xioctl(fd_, VIDIOC_QBUF, &held);      // a newer frame exists: hand the stale buffer straight back
        held = b;
        got = true;
    }
    if (!got) error.clear();
    return got;
}

CaptureSource::Result CaptureSource::read(std::vector<uint8_t>& rgba, int timeoutMs, std::string& error) {
    if (ps_) {
        RawFrame f;
        const auto r = ps_->readLatest(f, timeoutMs, error);
        if (r != remoteplay::Stream::Result::Frame) return r == remoteplay::Stream::Result::Timeout ? Result::Timeout : Result::Error;
        const int fmt = int(f.fmt);
        if (f.w != psConvW_ || f.h != psConvH_ || fmt != psConvFmt_) {
            if (!psConv_.init(f.fmt == RawFrame::P010 ? V4L2_PIX_FMT_P010 : V4L2_PIX_FMT_NV12, f.w, f.h, f.stride, error)) return Result::Error;
            psConvW_ = f.w; psConvH_ = f.h; psConvFmt_ = fmt;
        }
        w_ = f.w; h_ = f.h;
        if (!psConv_.convert(f.data.data(), f.data.size(), rgba, error)) return Result::Timeout;
        lastRepeat_ = false;
        probeRgba(rgba);
        return Result::Frame;
    }
    if (demo_) {
        RawFrame f;
        const auto r = demo_->readLatest(f, timeoutMs, error);
        if (r != demo::Stream::Result::Frame) return r == demo::Stream::Result::Timeout ? Result::Timeout : Result::Error;
        lastRepeat_ = false;
        probe_.feed(f.data.data() + 1, f.w, f.h, f.stride, 4, f.captured);
        rgba.swap(f.data);
        return Result::Frame;
    }
    if (sc_) {
        RawFrame f;
        const auto r = sc_->readLatest(f, timeoutMs, error);
        if (r != ScreenCastStream::Result::Frame) return r == ScreenCastStream::Result::Timeout ? Result::Timeout : Result::Error;
        w_ = f.w; h_ = f.h;
        lastRepeat_ = false;
        if (f.data.size() >= size_t(f.stride) * f.h) probe_.feed(f.data.data() + 1, f.w, f.h, f.stride, 4, f.captured);   // green ~ luma: measured picture rate
        rgba.swap(f.data);
        return Result::Frame;
    }
    if (fd_ < 0) { error = "Capture device is closed"; return Result::Error; }
    pollfd p{fd_, POLLIN, 0};
    const int r = poll(&p, 1, timeoutMs);
    if (r == 0) return Result::Timeout;
    if (r < 0) { if (errno == EINTR) return Result::Timeout; error = std::strerror(errno); return Result::Error; }
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { error = "Capture device disconnected"; return Result::Error; }
    v4l2_buffer b{};
    std::string e;
    if (!dequeueNewest(b, e)) { if (!e.empty()) { error = e; return Result::Error; } return Result::Timeout; }
    Result res = Result::Timeout;
    if (!(b.flags & V4L2_BUF_FLAG_ERROR) && b.bytesused > 0 && b.index < bufs_.size()) {
        std::string ce;
        if (conv_.convert(static_cast<const uint8_t*>(bufs_[b.index].start), b.bytesused, rgba, ce)) {
            res = Result::Frame;
            probeRgba(rgba);
            lastRepeat_ = !delivered_.changed(rgba.data() + 1, w_, h_, w_ * 4, 4, true);
        }
    }
    xioctl(fd_, VIDIOC_QBUF, &b);
    return res;
}

CaptureSource::Result CaptureSource::readLatest(RawFrame& out, int timeoutMs, std::string& error) {
    out.hold.reset();   // a driver buffer this (recycled) frame still held goes back to the card
    out.ext = nullptr;
    out.extSize = 0;
    out.fullRange = false;
    if (ps_) {
        out.repeat = false;
        const auto r = ps_->readLatest(out, timeoutMs, error);
        if (r == remoteplay::Stream::Result::Frame) {
            w_ = out.w; h_ = out.h;
            if (out.fmt == RawFrame::NV12) probe_.feed(out.data.data(), out.w, out.h, out.stride, 1, out.captured);   // luma plane
            else probe_.feed(out.data.data() + 1, out.w, out.h, out.stride, 2, out.captured);   // P010: high byte of luma
        }
        return r == remoteplay::Stream::Result::Frame ? Result::Frame : r == remoteplay::Stream::Result::Timeout ? Result::Timeout : Result::Error;
    }
    if (demo_) {
        const auto r = demo_->readLatest(out, timeoutMs, error);
        if (r == demo::Stream::Result::Frame) probe_.feed(out.data.data() + 1, out.w, out.h, out.stride, 4, out.captured);
        return r == demo::Stream::Result::Frame ? Result::Frame : r == demo::Stream::Result::Timeout ? Result::Timeout : Result::Error;
    }
    if (sc_) {
        const auto r = sc_->readLatest(out, timeoutMs, error);
        if (r == ScreenCastStream::Result::Frame) {
            w_ = out.w; h_ = out.h;
            if (out.data.size() >= size_t(out.stride) * out.h)
                probe_.feed(out.data.data() + 1, out.w, out.h, out.stride, 4, out.captured);   // measured picture rate
        }
        return r == ScreenCastStream::Result::Frame ? Result::Frame : r == ScreenCastStream::Result::Timeout ? Result::Timeout : Result::Error;
    }
    if (fd_ < 0) { error = "Capture device is closed"; return Result::Error; }
    pollfd p{fd_, POLLIN, 0};
    const int r = poll(&p, 1, timeoutMs);
    if (r == 0) return Result::Timeout;
    if (r < 0) { if (errno == EINTR) return Result::Timeout; error = std::strerror(errno); return Result::Error; }
    if (p.revents & (POLLERR | POLLHUP | POLLNVAL)) { error = "Capture device disconnected"; return Result::Error; }
    v4l2_buffer b{};
    std::string e;
    if (!dequeueNewest(b, e)) { if (!e.empty()) { error = e; return Result::Error; } return Result::Timeout; }
    Result res = Result::Timeout;
    out.captured = captureTime(b);
    if (!(b.flags & V4L2_BUF_FLAG_ERROR) && b.bytesused > 0 && b.index < bufs_.size()) {
        const uint8_t* src = static_cast<const uint8_t*>(bufs_[b.index].start);
        const size_t len = b.bytesused;
        const bool p010 = fourcc_ == V4L2_PIX_FMT_P010;
        const bool raw = fourcc_ == V4L2_PIX_FMT_YUYV || fourcc_ == V4L2_PIX_FMT_UYVY || fourcc_ == V4L2_PIX_FMT_NV12 || p010;
        if (raw) {
            const bool planar = fourcc_ == V4L2_PIX_FMT_NV12 || p010;
            const size_t need = planar ? size_t(rawStride_) * h_ * 3 / 2 : size_t(rawStride_) * h_;
            if (len >= need) {
                out.fmt = fourcc_ == V4L2_PIX_FMT_YUYV ? RawFrame::YUYV : fourcc_ == V4L2_PIX_FMT_UYVY ? RawFrame::UYVY
                        : p010 ? RawFrame::P010 : RawFrame::NV12;
                // no copy here: the renderer copies straight from the driver's buffer into the GPU upload buffer
                // (only with enough buffers: the renderer may hold two, the card must keep at least three)
                static const bool forceCopy = std::getenv("VEYAMUX_CAPTURE_COPY") != nullptr;   // the old way, to compare
                // The renderer may hold up to five (two waiting for their paced time, the mailbox, the picture on
                // screen, the frame being read); the card must always keep at least three to capture into.
                int held = 0;
                if (map_) { std::lock_guard l(map_->m); held = map_->held; }
                if (!map_ || forceCopy || held + 1 > int(bufs_.size()) - 3) {
                    out.data.assign(src, src + need);
                } else {
                    out.ext = src;
                    out.extSize = need;
                    // One owner of the buffer: it goes back to the card exactly once, when the last frame holding it
                    // lets go (not here, while the renderer still copies from it).
                    struct Lease {
                        std::shared_ptr<Mapping> map;
                        v4l2_buffer buf;
                        Lease(std::shared_ptr<Mapping> m, const v4l2_buffer& b) : map(std::move(m)), buf(b) {
                            std::lock_guard l(map->m);
                            ++map->held;
                        }
                        Lease(const Lease&) = delete;
                        Lease& operator=(const Lease&) = delete;
                        ~Lease() {
                            std::lock_guard l(map->m);
                            --map->held;
                            if (map->live) xioctl(map->fd, VIDIOC_QBUF, &buf);
                        }
                    };
                    out.hold = std::make_shared<Lease>(map_, b);
                }
                out.w = w_; out.h = h_; out.stride = rawStride_;
                out.fullRange = fullRange_;
                // luma bytes for repeat detection: NV12 every byte, YUYV every 2nd, UYVY every 2nd from 1, P010 the high bytes
                const int step = fourcc_ == V4L2_PIX_FMT_NV12 ? 1 : 2, off = (fourcc_ == V4L2_PIX_FMT_UYVY || p010) ? 1 : 0;
                static const bool sparse = std::getenv("VEYAMUX_DEDUP_SPARSE") != nullptr;
                if (sparse) out.repeat = !delivered_.changed(src + off, w_, h_, rawStride_, step, true);
                else { out.repeat = !newSinceDelivered_; newSinceDelivered_ = false; }   // see probeRaw
                res = Result::Frame;
            }
        } else if (isMjpeg(fourcc_) && !mjpegRgba_) {
            // MJPEG as NV12 (decoded on the graphics card when it can): 12 MB instead of 33 MB to upload at 4K, and
            // the colour conversion runs on the GPU
            std::string ce;
            int w = 0, h = 0, stride = 0;
            bool full = true, unsupported = false;
            if (conv_.decodeNv12(src, len, out.data, w, h, stride, full, &unsupported, ce)) {
                out.fmt = RawFrame::NV12;
                out.w = w; out.h = h; out.stride = stride;
                out.fullRange = full;
                const bool isNew = rawCompare_.changed(out.data.data(), w, h, stride, 1);
                probe_.feed(out.data.data(), w, h, stride, 1, out.captured, isNew ? 1 : 0);
                out.repeat = !isNew;
                res = Result::Frame;
            } else if (unsupported) {
                mjpegRgba_ = true;   // a JPEG layout NV12 cannot carry: RGBA from now on
            }
        } else {
            std::string ce;
            if (conv_.convert(src, len, out.data, ce)) {
                probeRgba(out.data);
                out.repeat = !delivered_.changed(out.data.data() + 1, w_, h_, w_ * 4, 4, true);
                out.fmt = RawFrame::RGBA;
                out.w = w_; out.h = h_; out.stride = w_ * 4;
                res = Result::Frame;
            }
        }
    }
    if (!out.hold) xioctl(fd_, VIDIOC_QBUF, &b);   // a leased buffer is queued again when its last frame lets go
    return res;
}

} // namespace veyamux
