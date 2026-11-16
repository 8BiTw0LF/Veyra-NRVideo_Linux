// VeyAmux for Linux: instant replay and recording (see Recorder.h).
#include "Recorder.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace veyamux {

namespace {
constexpr AVRational kVideoTb{1, 90000};
constexpr AVRational kAudioTb{1, 48000};
std::string averr(int e) { char b[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(e, b, sizeof b); return b; }
}

struct Recorder::Params {
    AVCodecParameters* v = avcodec_parameters_alloc();
    AVCodecParameters* a = nullptr;
    ~Params() { avcodec_parameters_free(&v); if (a) avcodec_parameters_free(&a); }
};

// One MP4 file being written. The file starts at the first video key frame; everything is shifted so that this
// frame has time 0, and audio from before it is dropped.
struct Recorder::Mux {
    AVFormatContext* oc = nullptr;
    int vi = -1, ai = -1;
    int64_t offV = -1;   // in kVideoTb
    bool ok = false;

    bool flv = false;
    bool open(const std::string& path, const Params& par, bool fragmented, std::string& err, const char* format = "mp4") {
        flv = std::strcmp(format, "flv") == 0;
        int e = avformat_alloc_output_context2(&oc, nullptr, format, path.c_str());
        if (e < 0 || !oc) { err = averr(e); return false; }
        AVStream* vs = avformat_new_stream(oc, nullptr);
        avcodec_parameters_copy(vs->codecpar, par.v);
        if (vs->codecpar->codec_id == AV_CODEC_ID_HEVC && !flv) vs->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
        else vs->codecpar->codec_tag = 0;
        vs->time_base = kVideoTb;
        vi = vs->index;
        if (par.a) {
            AVStream* as = avformat_new_stream(oc, nullptr);
            avcodec_parameters_copy(as->codecpar, par.a);
            as->codecpar->codec_tag = 0;
            as->time_base = kAudioTb;
            ai = as->index;
        }
        AVDictionary* io = nullptr;
        if (flv) av_dict_set(&io, "rw_timeout", "5000000", 0);   // a dead connection fails after 5 s instead of hanging
        e = avio_open2(&oc->pb, path.c_str(), AVIO_FLAG_WRITE, nullptr, &io);
        av_dict_free(&io);
        if (e < 0) { err = averr(e); close(); return false; }
        AVDictionary* o = nullptr;
        if (!flv) av_dict_set(&o, "movflags", fragmented ? "+frag_keyframe+empty_moov+default_base_moof" : "+faststart", 0);
        e = avformat_write_header(oc, &o);
        av_dict_free(&o);
        if (e < 0) { err = averr(e); close(); return false; }
        ok = true;
        return true;
    }
    // Takes a reference of p (p itself is left untouched). False when writing failed (stream: connection lost).
    bool write(const AVPacket* p, bool video, bool key) {
        if (!ok) return false;
        if (offV < 0) {
            if (!video || !key) return true;
            offV = p->pts;
        }
        if (!video && ai < 0) return true;
        const AVRational tb = video ? kVideoTb : kAudioTb;
        const int64_t off = video ? offV : av_rescale_q(offV, kVideoTb, kAudioTb);
        if (p->pts < off) return true;
        AVPacket* q = av_packet_clone(p);
        if (!q) return true;
        q->pts -= off;
        q->dts = (q->dts == AV_NOPTS_VALUE ? q->pts : q->dts - off);
        q->stream_index = video ? vi : ai;
        av_packet_rescale_ts(q, tb, oc->streams[q->stream_index]->time_base);
        const int e = av_interleaved_write_frame(oc, q);
        av_packet_free(&q);
        return e >= 0;
    }
    void close() {
        if (!oc) return;
        if (ok) av_write_trailer(oc);
        if (oc->pb) avio_closep(&oc->pb);
        avformat_free_context(oc);
        oc = nullptr;
        ok = false;
    }
    ~Mux() { close(); }
};

// The live stream: packets are handed to a sender thread (network writes can block); if it falls more than ~5 s
// behind, the backlog is dropped and sending resumes at the next key frame.
struct Recorder::StreamOut {
    Recorder* r = nullptr;
    std::string url;
    std::shared_ptr<Params> par;
    std::thread t;
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::pair<AVPacket*, bool>> q;   // packet, video
    bool quit = false, waitingKey = true;
    std::atomic<bool> failed{false};

    void run() {
        Mux mux;
        std::string err;
        if (!mux.open(url, *par, false, err, "flv")) {
            failed = true;
            r->message("Stream: could not connect (" + err + ")", true);
            return;
        }
        r->message("stream-started", false);
        for (;;) {
            std::pair<AVPacket*, bool> p{nullptr, false};
            {
                std::unique_lock l(m);
                cv.wait(l, [this] { return quit || !q.empty(); });
                if (q.empty()) break;
                p = q.front();
                q.pop_front();
            }
            const bool ok = mux.write(p.first, p.second, (p.first->flags & AV_PKT_FLAG_KEY) != 0);
            av_packet_free(&p.first);
            if (!ok) { failed = true; r->message("Stream: connection lost", true); break; }
        }
        mux.close();
    }
    void push(const AVPacket* p, bool video) {
        std::lock_guard l(m);
        if (q.size() > 600) {   // ~5 s behind
            for (auto& x : q) av_packet_free(&x.first);
            q.clear();
            waitingKey = true;
        }
        if (waitingKey) {
            if (!video || !(p->flags & AV_PKT_FLAG_KEY)) return;
            waitingKey = false;
        }
        if (AVPacket* c = av_packet_clone(p)) { q.emplace_back(c, video); cv.notify_one(); }
    }
    ~StreamOut() {
        { std::lock_guard l(m); quit = true; }
        cv.notify_all();
        if (t.joinable()) t.join();
        for (auto& x : q) av_packet_free(&x.first);
    }
};

Recorder::Recorder() { thread_ = std::thread([this] { worker(); }); }

void Recorder::startStream(const std::string& url, int kbps) {
    { std::lock_guard l(qm_); streamUrl_ = url; targetKbps_ = std::clamp(kbps, 1000, 50000); streamStartPending_ = true; streamStopPending_ = false; }
    streaming_ = true;
    qcv_.notify_all();
}

void Recorder::stopStream() {
    { std::lock_guard l(qm_); streamStopPending_ = true; streamStartPending_ = false; }
    streaming_ = false;
    qcv_.notify_all();
}

double Recorder::streamSeconds() const { return streamSec_.load(); }

void Recorder::closeVideoEncoder() {
    if (!venc_) return;
    avcodec_send_frame(venc_, nullptr);
    drainVideo();
    avcodec_free_context(&venc_);
    av_buffer_unref(&hwFrames_);
    av_frame_free(&swFrame_);
}

Recorder::~Recorder() {
    { std::lock_guard l(qm_); quit_ = true; recStopPending_ = true; }
    qcv_.notify_all();
    if (thread_.joinable()) thread_.join();
    { std::lock_guard l(saverM_); for (auto& t : savers_) if (t.joinable()) t.join(); }
    std::lock_guard l(ringM_);
    for (auto& p : ring_) av_packet_free(&p.p);
    ring_.clear();
}

void Recorder::setReplay(bool on, int seconds) {
    replaySeconds_ = std::clamp(seconds, 5, 600);
    replayOn_ = on;
    if (!on) {
        std::lock_guard l(ringM_);
        for (auto& p : ring_) av_packet_free(&p.p);
        ring_.clear();
    }
    qcv_.notify_all();
}

void Recorder::startRecording(const std::string& path) {
    { std::lock_guard l(qm_); recPath_ = path; recStartPending_ = true; recStopPending_ = false; }
    recording_ = true;
    qcv_.notify_all();
}

void Recorder::stopRecording() {
    { std::lock_guard l(qm_); recStopPending_ = true; recStartPending_ = false; }
    recording_ = false;
    qcv_.notify_all();
}

std::string Recorder::encoderName() const { std::lock_guard l(ringM_); return encName_; }
double Recorder::recordingSeconds() const { return recSec_.load(); }

void Recorder::pushVideo(std::vector<uint8_t>&& nv12, int w, int h, Clock::time_point t) {
    if (!active() || w < 2 || h < 2) return;
    std::lock_guard l(qm_);
    int pendingVideo = 0;
    for (auto& j : queue_) pendingVideo += j.audio ? 0 : 1;
    if (pendingVideo >= 8) return;   // the encoder is behind: drop this frame from the recording
    Job j;
    j.nv12 = std::move(nv12); j.w = w; j.h = h; j.t = t;
    queue_.push_back(std::move(j));
    qcv_.notify_one();
}

void Recorder::pushRgba(const uint8_t* rgba, int w, int h, int stride, int maxHeight, Clock::time_point t) {
    if (!active() || w < 2 || h < 2) return;
    std::lock_guard l(qm_);
    int pendingVideo = 0;
    for (auto& j : queue_) pendingVideo += j.audio ? 0 : 1;
    if (pendingVideo >= 4) return;
    Job j;
    j.rgba = true; j.w = w; j.h = h; j.maxH = maxHeight; j.t = t;
    j.nv12.resize(size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) std::memcpy(j.nv12.data() + size_t(y) * w * 4, rgba + size_t(y) * stride, size_t(w) * 4);
    queue_.push_back(std::move(j));
    qcv_.notify_one();
}

void Recorder::pushAudio(const int16_t* samples, int frames, Clock::time_point t) {
    if (!active() || frames <= 0) return;
    std::lock_guard l(qm_);
    if (queue_.size() > 400) return;
    Job j;
    j.audio = true; j.t = t;
    j.pcm.assign(samples, samples + size_t(frames) * 2);
    queue_.push_back(std::move(j));
    qcv_.notify_one();
}

// ------------------------------------------------------------------ worker

void Recorder::worker() {
    for (;;) {
        Job j;
        bool have = false, start = false, stop = false;
        std::string path;
        {
            std::unique_lock l(qm_);
            qcv_.wait_for(l, std::chrono::milliseconds(200), [this] { return quit_ || !queue_.empty() || recStartPending_ || recStopPending_; });
            if (recStopPending_) { stop = true; recStopPending_ = false; }
            if (recStartPending_ && venc_) { start = true; recStartPending_ = false; path = recPath_; }
            if (!queue_.empty()) { j = std::move(queue_.front()); queue_.pop_front(); have = true; }
            if (quit_ && queue_.empty() && !have) stop = true;
        }
        if (stop && rec_) {
            rec_->close();
            rec_.reset();
            message("recording-saved", false);
        }
        bool sStart = false, sStop = false;
        {
            std::lock_guard l(qm_);
            if (streamStopPending_ || quit_) { sStop = true; streamStopPending_ = false; }
            if (streamStartPending_) { sStart = true; }
        }
        if (sStop && stream_) {
            stream_.reset();
            wantH264_ = false;
            message("stream-stopped", false);
        }
        if (stream_ && stream_->failed.load()) {   // connection failed or lost
            stream_.reset();
            streaming_ = false;
            wantH264_ = false;
        }
        if (sStart) {
            wantH264_ = true;
            const bool ready = venc_ && encName_.find("hevc") == std::string::npos && venc_->bit_rate == int64_t(targetKbps_) * 1000;
            if (venc_ && !ready) {
                if (rec_) {   // the open recording uses another encoder setting
                    { std::lock_guard l(qm_); streamStartPending_ = false; }
                    streaming_ = false;
                    wantH264_ = false;
                    message("Stop the recording before starting a stream", true);
                } else {
                    closeVideoEncoder();   // reopened with H.264 at the stream bit rate on the next frame
                }
            } else if (ready) {
                { std::lock_guard l(qm_); streamStartPending_ = false; }
                std::shared_ptr<Params> par;
                { std::lock_guard l(ringM_); par = params_; }
                auto so = std::make_unique<StreamOut>();
                so->r = this;
                { std::lock_guard l(qm_); so->url = streamUrl_; }
                so->par = par;
                StreamOut* raw = so.get();
                so->t = std::thread([raw] { raw->run(); });
                stream_ = std::move(so);
                streamStart_ = Clock::now();
                forceKey_ = true;
            }
        }
        if (stream_) streamSec_ = std::chrono::duration<double>(Clock::now() - streamStart_).count();
        if (start) {
            std::shared_ptr<Params> par;
            { std::lock_guard l(ringM_); par = params_; }
            auto m = std::make_unique<Mux>();
            std::string err;
            if (par && m->open(path, *par, true, err)) {
                rec_ = std::move(m);
                recStart_ = Clock::now();
                recSec_ = 0;
                forceKey_ = true;
                message("recording-started", false);
            } else {
                recording_ = false;
                message("Could not open the file: " + err, true);
            }
        }
        if (have) {
            if (j.audio) encodeAudio(j);
            else if (!j.rgba || toNv12(j)) encodeVideo(j);
        }
        if (rec_) recSec_ = std::chrono::duration<double>(Clock::now() - recStart_).count();
        {
            std::lock_guard l(qm_);
            if (quit_ && queue_.empty()) break;
        }
        // Idle: nothing wants the encoder any more -> release it (and the VA-API device)
        if (!active() && venc_ && !rec_) {
            std::lock_guard l(qm_);
            if (queue_.empty()) closeEncoders();
        }
    }
    if (rec_) { rec_->close(); rec_.reset(); }
    closeEncoders();
    if (sws_) sws_freeContext(sws_);
}

bool Recorder::toNv12(Job& j) {
    int dw = j.w, dh = j.h;
    if (j.maxH > 0 && dh > j.maxH) { dw = int(std::lround(double(dw) * j.maxH / dh)); dh = j.maxH; }
    dw = std::max(2, dw & ~1); dh = std::max(2, dh & ~1);
    if (!sws_ || swsW_ != j.w || swsH_ != j.h || swsDW_ != dw || swsDH_ != dh) {
        if (sws_) sws_freeContext(sws_);
        sws_ = sws_getContext(j.w, j.h, AV_PIX_FMT_RGBA, dw, dh, AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr);
        swsW_ = j.w; swsH_ = j.h; swsDW_ = dw; swsDH_ = dh;
        if (!sws_) return false;
        const int* coef = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(sws_, coef, 1, coef, 0, 0, 1 << 16, 1 << 16);   // full-range RGB -> limited BT.709
    }
    std::vector<uint8_t> out(size_t(dw) * dh * 3 / 2);
    const uint8_t* src[1] = {j.nv12.data()};
    const int sstr[1] = {j.w * 4};
    uint8_t* dst[2] = {out.data(), out.data() + size_t(dw) * dh};
    const int dstr[2] = {dw, dw};
    sws_scale(sws_, src, sstr, 0, j.h, dst, dstr);
    j.nv12 = std::move(out);
    j.w = dw; j.h = dh; j.rgba = false;
    return true;
}

bool Recorder::openVideo(int w, int h, std::string& err) {
    if (!t0_.time_since_epoch().count()) t0_ = Clock::now() - std::chrono::seconds(2);
    const bool forceSw = std::getenv("VEYAMUX_REC_SOFTWARE") != nullptr;
    const double mpix = double(w) * h / 1e6;
    struct Try { const char* name; bool hw; };
    const Try tries[] = {{"hevc_vaapi", true}, {"h264_vaapi", true}, {"libx264", false}};
    for (const Try& t : tries) {
        if (t.hw && forceSw) continue;
        if (wantH264_ && std::strstr(t.name, "hevc")) continue;   // RTMP: H.264
        if (!t.hw && h > 1440 && !std::getenv("VEYAMUX_REC_SOFTWARE")) continue;   // too slow on the CPU above 1440p
        const AVCodec* codec = avcodec_find_encoder_by_name(t.name);
        if (!codec) continue;
        if (t.hw && !hwDevice_) {
            const char* dev = std::getenv("VEYAMUX_VAAPI_DEVICE");
            if (av_hwdevice_ctx_create(&hwDevice_, AV_HWDEVICE_TYPE_VAAPI, dev, nullptr, 0) < 0) { hwDevice_ = nullptr; continue; }
        }
        AVCodecContext* c = avcodec_alloc_context3(codec);
        c->width = w; c->height = h;
        c->time_base = kVideoTb;
        c->framerate = AVRational{60, 1};
        c->sample_aspect_ratio = AVRational{1, 1};
        c->gop_size = 60;
        c->max_b_frames = 0;
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        c->color_range = AVCOL_RANGE_MPEG;
        c->color_primaries = AVCOL_PRI_BT709;
        c->color_trc = AVCOL_TRC_BT709;
        c->colorspace = AVCOL_SPC_BT709;
        const bool hevc = std::strstr(t.name, "hevc") != nullptr;
        c->bit_rate = int64_t(std::clamp(mpix * (hevc ? 10.0 : 14.0), 4.0, 80.0) * 1e6);   // ~20 Mbit/s HEVC at 1080p60
        c->rc_max_rate = c->bit_rate * 3 / 2;
        c->rc_buffer_size = int(c->bit_rate);
        if (targetKbps_ > 0 && wantH264_) {   // live stream: constant bit rate
            c->bit_rate = c->rc_max_rate = int64_t(targetKbps_) * 1000;
            c->rc_buffer_size = int(c->bit_rate * 2);
        }
        AVBufferRef* frames = nullptr;
        if (t.hw) {
            frames = av_hwframe_ctx_alloc(hwDevice_);
            auto* fc = reinterpret_cast<AVHWFramesContext*>(frames->data);
            fc->format = AV_PIX_FMT_VAAPI;
            fc->sw_format = AV_PIX_FMT_NV12;
            fc->width = w; fc->height = h;
            fc->initial_pool_size = 16;
            if (av_hwframe_ctx_init(frames) < 0) { av_buffer_unref(&frames); avcodec_free_context(&c); continue; }
            c->pix_fmt = AV_PIX_FMT_VAAPI;
            c->hw_frames_ctx = av_buffer_ref(frames);
        } else {
            c->pix_fmt = AV_PIX_FMT_NV12;
            av_opt_set(c->priv_data, "preset", "veryfast", 0);
            av_opt_set(c->priv_data, "tune", "zerolatency", 0);
        }
        const int e = avcodec_open2(c, codec, nullptr);
        if (e < 0) {
            err = std::string(t.name) + ": " + averr(e);
            av_buffer_unref(&frames);
            avcodec_free_context(&c);
            continue;
        }
        venc_ = c;
        hwFrames_ = frames;
        vw_ = w; vh_ = h;
        ++gen_;
        lastVPts_ = -1;
        swFrame_ = av_frame_alloc();
        swFrame_->format = AV_PIX_FMT_NV12;
        swFrame_->width = w; swFrame_->height = h;
        av_frame_get_buffer(swFrame_, 0);
        { std::lock_guard l(ringM_); encName_ = t.name; }
        if (!aenc_) { std::string ae; if (!openAudio(ae)) message("Audio encoder: " + ae, true); }
        updateParams();
        return true;
    }
    if (err.empty()) err = "no usable video encoder (VA-API or libx264)";
    return false;
}

bool Recorder::openAudio(std::string& err) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!codec) { err = "no AAC encoder"; return false; }
    AVCodecContext* c = avcodec_alloc_context3(codec);
    c->sample_rate = 48000;
    c->sample_fmt = AV_SAMPLE_FMT_FLTP;
    AVChannelLayout st = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_copy(&c->ch_layout, &st);
    c->bit_rate = 192000;
    c->time_base = kAudioTb;
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    const int e = avcodec_open2(c, codec, nullptr);
    if (e < 0) { err = averr(e); avcodec_free_context(&c); return false; }
    aenc_ = c;
    afr_ = av_frame_alloc();
    afr_->format = AV_SAMPLE_FMT_FLTP;
    av_channel_layout_copy(&afr_->ch_layout, &st);
    afr_->sample_rate = 48000;
    afr_->nb_samples = c->frame_size > 0 ? c->frame_size : 1024;
    av_frame_get_buffer(afr_, 0);
    aSamples_ = -1;
    aPend_[0].clear(); aPend_[1].clear();
    return true;
}

void Recorder::updateParams() {
    auto p = std::make_shared<Params>();
    avcodec_parameters_from_context(p->v, venc_);
    if (aenc_) { p->a = avcodec_parameters_alloc(); avcodec_parameters_from_context(p->a, aenc_); }
    std::lock_guard l(ringM_);
    params_ = std::move(p);
}

void Recorder::closeEncoders() {
    if (venc_) { avcodec_send_frame(venc_, nullptr); drainVideo(); }
    if (rec_) { rec_->close(); rec_.reset(); recording_ = false; message("recording-saved", false); }
    avcodec_free_context(&venc_);
    avcodec_free_context(&aenc_);
    av_buffer_unref(&hwFrames_);
    av_buffer_unref(&hwDevice_);
    av_frame_free(&swFrame_);
    av_frame_free(&afr_);
    vw_ = vh_ = 0;
    t0_ = {};
    aSamples_ = -1;
}

void Recorder::encodeVideo(Job& j) {
    if (venc_ && (j.w != vw_ || j.h != vh_)) {
        // The picture size changed: a recording cannot continue in the same file, the replay restarts.
        if (rec_) { rec_->close(); rec_.reset(); recording_ = false; message("recording-saved", false); }
        if (stream_) { stream_.reset(); streaming_ = false; wantH264_ = false; message("Stream stopped: the picture size changed", true); }
        closeVideoEncoder();
    }
    if (!venc_) {
        std::string err;
        if (!openVideo(j.w, j.h, err)) {
            message("Video encoder: " + err, true);
            recording_ = false;
            replayOn_ = false;
            return;
        }
        if (recording_.load() && !rec_) { std::lock_guard l(qm_); if (!recStartPending_) recStartPending_ = true; }
    }
    const int64_t pts = av_rescale_q(std::chrono::duration_cast<std::chrono::microseconds>(j.t - t0_).count(), AVRational{1, 1000000}, kVideoTb);
    if (pts <= lastVPts_) return;   // same picture again (redraw) or out of order
    lastVPts_ = pts;
    if (av_frame_make_writable(swFrame_) < 0) return;
    for (int y = 0; y < j.h; ++y) std::memcpy(swFrame_->data[0] + size_t(y) * swFrame_->linesize[0], j.nv12.data() + size_t(y) * j.w, size_t(j.w));
    const uint8_t* uv = j.nv12.data() + size_t(j.w) * j.h;
    for (int y = 0; y < j.h / 2; ++y) std::memcpy(swFrame_->data[1] + size_t(y) * swFrame_->linesize[1], uv + size_t(y) * j.w, size_t(j.w));
    swFrame_->pts = pts;
    swFrame_->color_range = AVCOL_RANGE_MPEG;
    swFrame_->colorspace = AVCOL_SPC_BT709;
    swFrame_->pict_type = forceKey_ ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_NONE;
    forceKey_ = false;
    AVFrame* send = swFrame_;
    AVFrame* hw = nullptr;
    if (hwFrames_) {
        hw = av_frame_alloc();
        if (av_hwframe_get_buffer(hwFrames_, hw, 0) < 0 || av_hwframe_transfer_data(hw, swFrame_, 0) < 0) { av_frame_free(&hw); return; }
        av_frame_copy_props(hw, swFrame_);
        send = hw;
    }
    if (avcodec_send_frame(venc_, send) >= 0) drainVideo();
    av_frame_free(&hw);
}

void Recorder::drainVideo() {
    for (;;) {
        AVPacket* p = av_packet_alloc();
        if (avcodec_receive_packet(venc_, p) < 0) { av_packet_free(&p); return; }
        deliver(p, true);
    }
}

void Recorder::encodeAudio(Job& j) {
    if (!venc_ || !aenc_) return;   // audio follows the video session
    const int frames = int(j.pcm.size() / 2);
    const int64_t wall = av_rescale_q(std::chrono::duration_cast<std::chrono::microseconds>(j.t - t0_).count(), AVRational{1, 1000000}, kAudioTb);
    const int64_t pendingNow = aSamples_ + int64_t(aPend_[0].size());
    if (aSamples_ < 0 || std::llabs(pendingNow - wall) > 4800) {   // start, or drifted > 100 ms: resync
        aPend_[0].clear(); aPend_[1].clear();
        aSamples_ = std::max<int64_t>(0, wall);
    }
    for (int i = 0; i < frames; ++i) {
        aPend_[0].push_back(j.pcm[size_t(i) * 2] / 32768.0f);
        aPend_[1].push_back(j.pcm[size_t(i) * 2 + 1] / 32768.0f);
    }
    const int n = afr_->nb_samples;
    while (int(aPend_[0].size()) >= n) {
        if (av_frame_make_writable(afr_) < 0) return;
        for (int ch = 0; ch < 2; ++ch) {
            std::memcpy(afr_->data[ch], aPend_[ch].data(), size_t(n) * sizeof(float));
            aPend_[ch].erase(aPend_[ch].begin(), aPend_[ch].begin() + n);
        }
        afr_->pts = aSamples_;
        aSamples_ += n;
        if (avcodec_send_frame(aenc_, afr_) >= 0) drainAudio();
    }
}

void Recorder::drainAudio() {
    for (;;) {
        AVPacket* p = av_packet_alloc();
        if (avcodec_receive_packet(aenc_, p) < 0) { av_packet_free(&p); return; }
        deliver(p, false);
    }
}

void Recorder::deliver(AVPacket* p, bool video) {
    const bool key = (p->flags & AV_PKT_FLAG_KEY) != 0;
    if (rec_) rec_->write(p, video, key);
    if (stream_ && !stream_->failed.load()) stream_->push(p, video);
    if (replayOn_.load()) {
        Pkt k;
        k.p = p; k.video = video; k.key = key; k.gen = video ? gen_ : 0;
        k.sec = double(p->pts) * (video ? av_q2d(kVideoTb) : av_q2d(kAudioTb));
        std::lock_guard l(ringM_);
        ring_.push_back(k);
        trimRing();
    } else {
        av_packet_free(&p);
    }
}

// Keeps replaySeconds + a little more, always starting at a video key frame.
void Recorder::trimRing() {
    if (ring_.empty()) return;
    const double newest = ring_.back().sec;
    const double keep = replaySeconds_.load() + 2.0;
    while (!ring_.empty() && newest - ring_.front().sec > keep) {
        // find the next video key frame after the front; only drop up to it
        size_t next = 1;
        while (next < ring_.size() && !(ring_[next].video && ring_[next].key)) ++next;
        if (next >= ring_.size() || newest - ring_[next].sec < replaySeconds_.load()) break;
        for (size_t i = 0; i < next; ++i) av_packet_free(&ring_[i].p);
        ring_.erase(ring_.begin(), ring_.begin() + long(next));
    }
}

bool Recorder::saveReplay(const std::string& path) {
    std::vector<Pkt> copy;
    std::shared_ptr<Params> par;
    {
        std::lock_guard l(ringM_);
        if (ring_.empty() || !params_) return false;
        par = params_;
        uint64_t gen = 0;
        for (auto it = ring_.rbegin(); it != ring_.rend(); ++it) if (it->video) { gen = it->gen; break; }
        const double newest = ring_.back().sec, from = newest - replaySeconds_.load();
        // start at the last key frame at or before 'from' (or the first one)
        size_t start = ring_.size();
        for (size_t i = 0; i < ring_.size(); ++i) {
            if (!(ring_[i].video && ring_[i].key && ring_[i].gen == gen)) continue;
            if (start == ring_.size() || ring_[i].sec <= from) start = i;
            if (ring_[i].sec > from) break;
        }
        if (start == ring_.size()) return false;
        for (size_t i = start; i < ring_.size(); ++i) {
            if (ring_[i].video && ring_[i].gen != gen) continue;
            Pkt k = ring_[i];
            k.p = av_packet_clone(ring_[i].p);
            if (k.p) copy.push_back(k);
        }
    }
    if (copy.empty()) return false;
    std::lock_guard l(saverM_);
    savers_.erase(std::remove_if(savers_.begin(), savers_.end(), [](std::thread& t) { return !t.joinable(); }), savers_.end());
    savers_.emplace_back([this, path, par, copy = std::move(copy)]() mutable {
        Mux m;
        std::string err;
        bool ok = m.open(path, *par, false, err);
        for (auto& k : copy) {
            if (ok) m.write(k.p, k.video, k.key);
            av_packet_free(&k.p);
        }
        m.close();
        message(ok ? "replay-saved:" + path : "Could not save the replay: " + err, !ok);
    });
    return true;
}

} // namespace veyamux
