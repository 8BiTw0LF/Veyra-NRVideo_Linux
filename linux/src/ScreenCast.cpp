// VeyAmux for Linux: window / screen source through xdg-desktop-portal + PipeWire (see ScreenCast.h).
#include "ScreenCast.h"
#include "CaptureSource.h"


#include <pipewire/pipewire.h>
#include <spa/param/video/format-utils.h>
#include <spa/param/video/type-info.h>
#include <spa/param/buffers.h>
#include <spa/param/param.h>
#include <spa/buffer/meta.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace veyamux {

// ------------------------------------------------------------------ libpipewire, loaded at run time

namespace {

struct Pw {
    bool ok = false;
    std::string why;
#define VX_PW(name) decltype(&::name) name = nullptr;
    VX_PW(pw_init)
    VX_PW(pw_thread_loop_new)
    VX_PW(pw_thread_loop_get_loop)
    VX_PW(pw_thread_loop_start)
    VX_PW(pw_thread_loop_stop)
    VX_PW(pw_thread_loop_lock)
    VX_PW(pw_thread_loop_unlock)
    VX_PW(pw_thread_loop_destroy)
    VX_PW(pw_context_new)
    VX_PW(pw_context_connect)
    VX_PW(pw_context_connect_fd)
    VX_PW(pw_context_destroy)
    VX_PW(pw_core_disconnect)
    VX_PW(pw_properties_new)
    VX_PW(pw_stream_new)
    VX_PW(pw_stream_add_listener)
    VX_PW(pw_stream_connect)
    VX_PW(pw_stream_disconnect)
    VX_PW(pw_stream_destroy)
    VX_PW(pw_stream_dequeue_buffer)
    VX_PW(pw_stream_queue_buffer)
    VX_PW(pw_stream_update_params)
#undef VX_PW
};

const Pw& pw() {
    static Pw p = [] {
        Pw p;
        void* h = dlopen("libpipewire-0.3.so.0", RTLD_NOW | RTLD_LOCAL);
        if (!h) { p.why = "PipeWire (libpipewire-0.3.so.0) was not found"; return p; }
        bool all = true;
#define VX_PW(name) p.name = reinterpret_cast<decltype(p.name)>(dlsym(h, #name)); all = all && p.name;
        VX_PW(pw_init)
        VX_PW(pw_thread_loop_new)
        VX_PW(pw_thread_loop_get_loop)
        VX_PW(pw_thread_loop_start)
        VX_PW(pw_thread_loop_stop)
        VX_PW(pw_thread_loop_lock)
        VX_PW(pw_thread_loop_unlock)
        VX_PW(pw_thread_loop_destroy)
        VX_PW(pw_context_new)
        VX_PW(pw_context_connect)
        VX_PW(pw_context_connect_fd)
        VX_PW(pw_context_destroy)
        VX_PW(pw_core_disconnect)
        VX_PW(pw_properties_new)
        VX_PW(pw_stream_new)
        VX_PW(pw_stream_add_listener)
        VX_PW(pw_stream_connect)
        VX_PW(pw_stream_disconnect)
        VX_PW(pw_stream_destroy)
        VX_PW(pw_stream_dequeue_buffer)
        VX_PW(pw_stream_queue_buffer)
        VX_PW(pw_stream_update_params)
#undef VX_PW
        if (!all) { p.why = "PipeWire is too old (missing functions)"; return p; }
        p.pw_init(nullptr, nullptr);
        p.ok = true;
        return p;
    }();
    return p;
}

} // namespace

bool ScreenCastStream::available(std::string* why) {
    const Pw& p = pw();
    if (!p.ok && why) *why = p.why;
    return p.ok;
}

// ------------------------------------------------------------------ the stream

struct ScreenCastStream::Impl {
    pw_thread_loop* loop = nullptr;
    pw_context* ctx = nullptr;
    pw_core* core = nullptr;
    pw_stream* stream = nullptr;
    spa_hook listener{};
    pw_stream_events events{};
};

ScreenCastStream::ScreenCastStream() = default;
ScreenCastStream::~ScreenCastStream() { close(); }

namespace {
void cbState(void* d, pw_stream_state, pw_stream_state s, const char* err) { static_cast<ScreenCastStream*>(d)->onState(int(s), err); }
void cbParam(void* d, uint32_t id, const spa_pod* p) { if (id == SPA_PARAM_Format && p) static_cast<ScreenCastStream*>(d)->onFormat(p); }
void cbProcess(void* d) { static_cast<ScreenCastStream*>(d)->onProcess(); }
}

bool ScreenCastStream::open(int fd, uint32_t node, std::string& error) {
    close();
    const Pw& p = pw();
    if (!p.ok) { error = p.why; return false; }
    {
        std::lock_guard l(m_);
        latest_.clear(); fresh_ = false; failed_ = false; failMsg_.clear(); lw_ = lh_ = 0;
        fmt_ = 0; fw_ = fh_ = 0;
    }
    d_ = new Impl;
    d_->loop = p.pw_thread_loop_new("veyamux-screencast", nullptr);
    if (!d_->loop) { error = "PipeWire: cannot create a thread loop"; close(); return false; }
    d_->ctx = p.pw_context_new(p.pw_thread_loop_get_loop(d_->loop), nullptr, 0);
    if (!d_->ctx) { error = "PipeWire: cannot create a context"; close(); return false; }
    if (p.pw_thread_loop_start(d_->loop) < 0) { error = "PipeWire: cannot start the thread loop"; close(); return false; }

    p.pw_thread_loop_lock(d_->loop);
    if (fd >= 0) {
        const int own = fcntl(fd, F_DUPFD_CLOEXEC, 3);   // PipeWire takes ownership of the fd it gets
        d_->core = own >= 0 ? p.pw_context_connect_fd(d_->ctx, own, nullptr, 0) : nullptr;
    } else {
        d_->core = p.pw_context_connect(d_->ctx, nullptr, 0);
    }
    if (!d_->core) { p.pw_thread_loop_unlock(d_->loop); error = "PipeWire: cannot connect to the screen share"; close(); return false; }

    d_->stream = p.pw_stream_new(d_->core, "VeyAmux screen share",
                                 p.pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                                                     PW_KEY_MEDIA_ROLE, "Screen", nullptr));
    if (!d_->stream) { p.pw_thread_loop_unlock(d_->loop); error = "PipeWire: cannot create a stream"; close(); return false; }
    d_->events.version = PW_VERSION_STREAM_EVENTS;
    d_->events.state_changed = cbState;
    d_->events.param_changed = cbParam;
    d_->events.process = cbProcess;
    p.pw_stream_add_listener(d_->stream, &d_->listener, &d_->events, this);

    // Shared-memory RGB(A) only (no DMA-BUF modifiers offered, so the compositor sends memfd buffers); RGBA order
    // first because it goes to the GPU unchanged.
    uint8_t buf[1024];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    const spa_rectangle defSize{1920, 1080}, minSize{1, 1}, maxSize{8192, 8192};
    const spa_fraction defRate{60, 1}, minRate{0, 1}, maxRate{360, 1};
    const spa_pod* params[1];
    params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_CHOICE_ENUM_Id(5, SPA_VIDEO_FORMAT_RGBA, SPA_VIDEO_FORMAT_RGBA, SPA_VIDEO_FORMAT_RGBx,
                                                         SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_BGRx),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&defSize, &minSize, &maxSize),
        // compositors send 0/1 (variable, only on change) with a maximum rate; accept a fixed rate as well
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&defRate, &minRate, &maxRate),
        SPA_FORMAT_VIDEO_maxFramerate, SPA_POD_CHOICE_RANGE_Fraction(&defRate, &minRate, &maxRate)));
    const int r = p.pw_stream_connect(d_->stream, PW_DIRECTION_INPUT, fd >= 0 || node != 0 ? node : PW_ID_ANY,
                                      pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS | PW_STREAM_FLAG_DONT_RECONNECT),
                                      params, 1);
    p.pw_thread_loop_unlock(d_->loop);
    if (r < 0) { error = std::string("PipeWire: cannot connect the stream (") + spa_strerror(r) + ")"; close(); return false; }

    // wait for the first picture (its size decides the processing size)
    std::unique_lock l(m_);
    cv_.wait_for(l, std::chrono::seconds(5), [&] { return fresh_ || failed_; });
    if (failed_ || !fresh_) {
        error = failed_ ? failMsg_ : "The screen share sends no picture";
        l.unlock();
        close();
        return false;
    }
    w_ = lw_; h_ = lh_;
    return true;
}

void ScreenCastStream::close() {
    if (!d_) return;
    const Pw& p = pw();
    if (d_->loop) p.pw_thread_loop_stop(d_->loop);
    if (d_->stream) { p.pw_stream_disconnect(d_->stream); p.pw_stream_destroy(d_->stream); }
    if (d_->core) p.pw_core_disconnect(d_->core);
    if (d_->ctx) p.pw_context_destroy(d_->ctx);
    if (d_->loop) p.pw_thread_loop_destroy(d_->loop);
    delete d_;
    d_ = nullptr;
    std::lock_guard l(m_);
    failed_ = true;
    failMsg_ = "closed";
    cv_.notify_all();
}

void ScreenCastStream::onState(int state, const char* error) {
    // the shared window/screen went away: DONT_RECONNECT ends the stream instead of waiting for another source
    if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED) {
        std::lock_guard l(m_);
        failed_ = true;
        // after pictures have arrived, an error means the source went away (window closed, share stopped)
        failMsg_ = state == PW_STREAM_STATE_ERROR && lw_ == 0 ? std::string("Screen share error: ") + (error ? error : "?")
                                                              : "The shared window was closed";
        cv_.notify_all();
    }
}

void ScreenCastStream::onFormat(const void* podv) {
    const spa_pod* pod = static_cast<const spa_pod*>(podv);
    uint32_t mt = 0, mst = 0;
    if (spa_format_parse(pod, &mt, &mst) < 0 || mt != SPA_MEDIA_TYPE_video || mst != SPA_MEDIA_SUBTYPE_raw) return;
    spa_video_info_raw info{};
    if (spa_format_video_raw_parse(pod, &info) < 0) return;
    fmt_ = info.format;
    fw_ = int(info.size.width);
    fh_ = int(info.size.height);
    // the most the compositor sends (usually the display's refresh rate); pictures only come when something changes
    if (info.max_framerate.denom && info.max_framerate.num) fps_ = double(info.max_framerate.num) / info.max_framerate.denom;
    else if (info.framerate.denom && info.framerate.num) fps_ = double(info.framerate.num) / info.framerate.denom;
    if (fps_ <= 0 || fps_ > 360) fps_ = 60;

    // buffers: shared memory, mapped by PipeWire; a crop rectangle for windows that are smaller than the buffer
    uint8_t buf[512];
    spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof buf);
    const spa_pod* params[2];
    params[0] = static_cast<const spa_pod*>(spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(4, 2, 8),
        SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int((1 << SPA_DATA_MemPtr) | (1 << SPA_DATA_MemFd))));
    params[1] = static_cast<const spa_pod*>(spa_pod_builder_add_object(&b,
        SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta,
        SPA_PARAM_META_type, SPA_POD_Id(SPA_META_VideoCrop),
        SPA_PARAM_META_size, SPA_POD_Int(sizeof(spa_meta_region))));
    pw().pw_stream_update_params(d_->stream, params, 2);
}

void ScreenCastStream::onProcess() {
    const Pw& p = pw();
    // newest buffer only
    pw_buffer* newest = nullptr;
    while (pw_buffer* bf = p.pw_stream_dequeue_buffer(d_->stream)) {
        if (newest) p.pw_stream_queue_buffer(d_->stream, newest);
        newest = bf;
    }
    if (!newest) return;
    spa_buffer* sb = newest->buffer;
    const spa_data& dd = sb->datas[0];
    const bool rgbaOrder = fmt_ == SPA_VIDEO_FORMAT_RGBA || fmt_ == SPA_VIDEO_FORMAT_RGBx;
    const bool known = rgbaOrder || fmt_ == SPA_VIDEO_FORMAT_BGRA || fmt_ == SPA_VIDEO_FORMAT_BGRx;
    if (known && dd.data && dd.chunk && dd.chunk->size > 0 && !(dd.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED) && fw_ > 0 && fh_ > 0) {
        int x0 = 0, y0 = 0, w = fw_, h = fh_;
        if (auto* crop = static_cast<spa_meta_region*>(spa_buffer_find_meta_data(sb, SPA_META_VideoCrop, sizeof(spa_meta_region)))) {
            if (crop->region.size.width > 0 && crop->region.size.height > 0) {
                x0 = std::clamp(crop->region.position.x, 0, fw_ - 1);
                y0 = std::clamp(crop->region.position.y, 0, fh_ - 1);
                w = std::min(int(crop->region.size.width), fw_ - x0);
                h = std::min(int(crop->region.size.height), fh_ - y0);
            }
        }
        w &= ~1; h &= ~1;   // even sizes (video encoders, NV12)
        const int stride = dd.chunk->stride > 0 ? dd.chunk->stride : fw_ * 4;
        const uint8_t* src = static_cast<const uint8_t*>(dd.data) + dd.chunk->offset;
        const size_t avail = dd.maxsize > dd.chunk->offset ? dd.maxsize - dd.chunk->offset : 0;
        if (w >= 2 && h >= 2 && size_t(y0 + h - 1) * stride + size_t(x0 + w) * 4 <= avail) {
            // written into a buffer of this thread's own; the newest finished picture (latest_) stays intact until
            // it is swapped below, so the reader never gets a buffer that is being written
            std::vector<uint8_t>& out = spare_;
            out.resize(size_t(w) * h * 4);
            for (int y = 0; y < h; ++y) {
                const uint8_t* s = src + size_t(y0 + y) * stride + size_t(x0) * 4;
                uint8_t* o = out.data() + size_t(y) * w * 4;
                if (rgbaOrder) {
                    std::memcpy(o, s, size_t(w) * 4);
                } else {
                    for (int x = 0; x < w; ++x) { o[4 * x] = s[4 * x + 2]; o[4 * x + 1] = s[4 * x + 1]; o[4 * x + 2] = s[4 * x]; o[4 * x + 3] = 255; }
                }
            }
            std::lock_guard l(m_);
            latest_.swap(out);           // spare_ now holds the previous picture (or the reader's old buffer)
            lw_ = w; lh_ = h;
            fresh_ = true;
            stamp_ = std::chrono::steady_clock::now();
            cv_.notify_all();
        }
    }
    p.pw_stream_queue_buffer(d_->stream, newest);
}

ScreenCastStream::Result ScreenCastStream::readLatest(RawFrame& out, int timeoutMs, std::string& error) {
    std::unique_lock l(m_);
    if (!cv_.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return fresh_ || failed_; })) return Result::Timeout;
    if (failed_) { error = failMsg_; return Result::Error; }
    fresh_ = false;
    if (latest_.size() < size_t(lw_) * lh_ * 4) return Result::Timeout;   // never hand out a short buffer
    out.fmt = RawFrame::RGBA;
    out.w = lw_; out.h = lh_; out.stride = lw_ * 4;
    out.repeat = false;
    out.captured = stamp_;
    out.data.swap(latest_);
    w_ = lw_; h_ = lh_;
    return Result::Frame;
}

} // namespace veyamux
