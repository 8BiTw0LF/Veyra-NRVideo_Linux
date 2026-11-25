// VeyAmux for Linux: subtitles (see Subtitles.h).
#include "Subtitles.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

#include <algorithm>
#include <cstring>

namespace veyamux {

namespace {

// ASS dialogue as FFmpeg hands it: "ReadOrder,Layer,Style,Name,MarginL,MarginR,MarginV,Effect,Text"
std::string assText(const char* ass) {
    std::string s = ass ? ass : "";
    if (s.rfind("Dialogue:", 0) == 0) {   // an old FFmpeg: the full line
        int commas = 0;
        size_t i = 0;
        for (; i < s.size() && commas < 9; ++i) if (s[i] == ',') ++commas;
        s = s.substr(i);
    } else {
        int commas = 0;
        size_t i = 0;
        for (; i < s.size() && commas < 8; ++i) if (s[i] == ',') ++commas;
        s = s.substr(i);
    }
    // {\i1}-style codes out, line breaks in
    std::string out;
    bool inCode = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '{') { inCode = true; continue; }
        if (c == '}') { inCode = false; continue; }
        if (inCode) continue;
        if (c == '\\' && i + 1 < s.size() && (s[i + 1] == 'N' || s[i + 1] == 'n')) { out += '\n'; ++i; continue; }
        if (c == '\\' && i + 1 < s.size() && s[i + 1] == 'h') { out += ' '; ++i; continue; }
        if (c == '\r') continue;
        out += c;
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
    return out;
}

std::string tag(AVDictionary* d, const char* key) {
    const AVDictionaryEntry* e = av_dict_get(d, key, nullptr, 0);
    return e ? e->value : "";
}

AVCodecContext* openDecoder(const AVCodecParameters* par, double timeBaseNum, double timeBaseDen, int videoW, int videoH) {
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (!codec) return nullptr;
    AVCodecContext* c = avcodec_alloc_context3(codec);
    if (!c) return nullptr;
    avcodec_parameters_to_context(c, par);
    c->pkt_timebase = AVRational{int(timeBaseNum), int(timeBaseDen)};
    if (c->width <= 0 && videoW > 0) { c->width = videoW; c->height = videoH; }
    AVDictionary* o = nullptr;
    av_dict_set(&o, "sub_text_format", "ass", 0);
    if (avcodec_open2(c, codec, &o) < 0) { av_dict_free(&o); avcodec_free_context(&c); return nullptr; }
    av_dict_free(&o);
    return c;
}

} // namespace

Subtitles::~Subtitles() { reset(); }

void Subtitles::reset() {
    std::lock_guard l(m_);
    if (dec_) avcodec_free_context(&dec_);
    tracks_.clear();
    cues_.clear();
    timeBase_.clear();
    fmt_ = nullptr;
    selected_ = -1;
    nextExternal_ = 1000;
}

void Subtitles::addStream(AVFormatContext* fmt, int index, int videoW, int videoH) {
    const AVStream* st = fmt->streams[index];
    std::lock_guard l(m_);
    fmt_ = fmt;
    startTime_ = fmt->start_time != AV_NOPTS_VALUE ? double(fmt->start_time) / AV_TIME_BASE : 0.0;
    videoW_ = videoW; videoH_ = videoH;
    SubtitleTrack t;
    t.id = index;
    t.lang = tag(st->metadata, "language");
    t.title = tag(st->metadata, "title");
    t.codec = avcodec_get_name(st->codecpar->codec_id);
    const AVCodecDescriptor* d = avcodec_descriptor_get(st->codecpar->codec_id);
    t.bitmap = d && (d->props & AV_CODEC_PROP_BITMAP_SUB);
    t.forced = st->disposition & AV_DISPOSITION_FORCED;
    t.isDefault = st->disposition & AV_DISPOSITION_DEFAULT;
    if (!avcodec_find_decoder(st->codecpar->codec_id)) return;   // e.g. teletext without a decoder
    tracks_.push_back(t);
    timeBase_[index] = av_q2d(st->time_base);
}

int Subtitles::addFile(const std::string& path, const std::string& lang, const std::string& title, std::string& error) {
    AVFormatContext* f = nullptr;
    AVDictionary* o = nullptr;
    av_dict_set(&o, "protocol_whitelist", "file", 0);
    int r = avformat_open_input(&f, path.c_str(), nullptr, &o);
    av_dict_free(&o);
    if (r < 0) { error = "Could not open the subtitles"; return -1; }
    avformat_find_stream_info(f, nullptr);
    const int si = av_find_best_stream(f, AVMEDIA_TYPE_SUBTITLE, -1, -1, nullptr, 0);
    if (si < 0) { avformat_close_input(&f); error = "No subtitles in the file"; return -1; }
    AVStream* st = f->streams[si];
    int vw, vh;
    { std::lock_guard l(m_); vw = videoW_; vh = videoH_; }
    AVCodecContext* dec = openDecoder(st->codecpar, st->time_base.num, st->time_base.den, vw, vh);
    if (!dec) { avformat_close_input(&f); error = "These subtitles cannot be read"; return -1; }
    std::vector<SubtitleCue> list;
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(f, pkt) >= 0) {
        if (pkt->stream_index == si) decode(dec, pkt, av_q2d(st->time_base), 0.0, list, vw, vh);
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    SubtitleTrack t;
    t.lang = lang;
    t.title = title;
    t.codec = avcodec_get_name(st->codecpar->codec_id);
    t.external = true;
    avcodec_free_context(&dec);
    avformat_close_input(&f);
    if (list.empty()) { error = "The subtitle file is empty"; return -1; }
    std::sort(list.begin(), list.end(), [](const SubtitleCue& a, const SubtitleCue& b) { return a.start < b.start; });
    std::lock_guard l(m_);
    t.id = nextExternal_++;
    tracks_.push_back(t);
    cues_[t.id] = std::move(list);
    return t.id;
}

std::vector<SubtitleTrack> Subtitles::tracks() const { std::lock_guard l(m_); return tracks_; }

bool Subtitles::select(int id) {
    std::lock_guard l(m_);
    if (dec_) avcodec_free_context(&dec_);
    selected_ = -1;
    if (id < 0) return true;
    const auto t = std::find_if(tracks_.begin(), tracks_.end(), [id](const SubtitleTrack& x) { return x.id == id; });
    if (t == tracks_.end()) return false;
    if (!t->external) {
        if (!fmt_ || id >= int(fmt_->nb_streams)) return false;
        const AVStream* st = fmt_->streams[id];
        dec_ = openDecoder(st->codecpar, st->time_base.num, st->time_base.den, videoW_, videoH_);
        if (!dec_) return false;
        cues_[id].clear();
    }
    selected_ = id;
    return true;
}

int Subtitles::selected() const { std::lock_guard l(m_); return selected_; }
int Subtitles::selectedStream() const { std::lock_guard l(m_); return dec_ ? selected_ : -1; }

void Subtitles::feed(const AVPacket* pkt) {
    std::lock_guard l(m_);
    if (!dec_ || pkt->stream_index != selected_) return;
    auto& list = cues_[selected_];
    decode(dec_, pkt, timeBase_[selected_], startTime_, list, videoW_, videoH_);
    // a long film: cues more than a minute behind the newest are dropped (pictures take memory)
    if (list.size() > 400) list.erase(list.begin(), list.begin() + 100);
}

void Subtitles::flush() {
    std::lock_guard l(m_);
    if (dec_) avcodec_flush_buffers(dec_);
    if (selected_ >= 0 && selected_ < 1000) cues_[selected_].clear();
}

// called with m_ held (or on a private list)
void Subtitles::decode(AVCodecContext* dec, const AVPacket* pkt, double timeBase, double startTime, std::vector<SubtitleCue>& list, int frameW,
                       int frameH) {
    AVSubtitle sub{};
    int got = 0;
    AVPacket* p = av_packet_clone(pkt);
    if (!p) return;
    const int r = avcodec_decode_subtitle2(dec, &sub, &got, p);
    av_packet_free(&p);
    if (r < 0 || !got) return;
    const int64_t pts = sub.pts != AV_NOPTS_VALUE ? av_rescale_q(sub.pts, AV_TIME_BASE_Q, AVRational{1, 1000000})
                                                : int64_t(double(pkt->pts) * timeBase * 1e6);
    const double base = double(pts) / 1e6 - startTime;
    const double start = base + sub.start_display_time / 1000.0;
    double end = sub.end_display_time > 0 && sub.end_display_time != UINT32_MAX ? base + sub.end_display_time / 1000.0 : 0.0;
    if (end <= start && pkt->duration > 0) end = start + double(pkt->duration) * timeBase;
    // a picture subtitle often has no end: the next one (or an empty one) ends it
    for (auto it = list.rbegin(); it != list.rend(); ++it) {
        if (it->start <= start && (it->end <= it->start || it->end > start) && !it->rgba.empty()) { it->end = start; break; }
        if (it->start <= start) break;
    }
    if (sub.num_rects == 0) { avsubtitle_free(&sub); return; }   // only clears the screen
    SubtitleCue c;
    c.start = start;
    c.end = end > start ? end : 0.0;   // 0: until the next one
    c.id = nextId_++;
    int x0 = INT32_MAX, y0 = INT32_MAX, x1 = 0, y1 = 0;
    for (unsigned i = 0; i < sub.num_rects; ++i) {
        const AVSubtitleRect* rc = sub.rects[i];
        if (rc->type == SUBTITLE_ASS && rc->ass) {
            const std::string t = assText(rc->ass);
            if (!t.empty()) c.text += (c.text.empty() ? "" : "\n") + t;
        } else if (rc->type == SUBTITLE_TEXT && rc->text) {
            c.text += (c.text.empty() ? "" : "\n") + std::string(rc->text);
        } else if (rc->type == SUBTITLE_BITMAP && rc->w > 0 && rc->h > 0) {
            x0 = std::min(x0, rc->x); y0 = std::min(y0, rc->y);
            x1 = std::max(x1, rc->x + rc->w); y1 = std::max(y1, rc->y + rc->h);
        }
    }
    if (x1 > x0 && y1 > y0) {
        c.x = x0; c.y = y0; c.w = x1 - x0; c.h = y1 - y0;
        c.frameW = dec->width > 0 ? dec->width : frameW;
        c.frameH = dec->height > 0 ? dec->height : frameH;
        c.rgba.assign(size_t(c.w) * c.h * 4, 0);
        for (unsigned i = 0; i < sub.num_rects; ++i) {
            const AVSubtitleRect* rc = sub.rects[i];
            if (rc->type != SUBTITLE_BITMAP || rc->w <= 0 || !rc->data[0] || !rc->data[1]) continue;
            const auto* pal = reinterpret_cast<const uint32_t*>(rc->data[1]);   // AARRGGBB
            for (int y = 0; y < rc->h; ++y)
                for (int x = 0; x < rc->w; ++x) {
                    const uint32_t v = pal[rc->data[0][size_t(y) * rc->linesize[0] + x]];
                    uint8_t* o = &c.rgba[(size_t(rc->y - y0 + y) * c.w + size_t(rc->x - x0 + x)) * 4];
                    o[0] = uint8_t(v >> 16); o[1] = uint8_t(v >> 8); o[2] = uint8_t(v); o[3] = uint8_t(v >> 24);
                }
        }
    }
    avsubtitle_free(&sub);
    if (c.text.empty() && c.rgba.empty()) return;
    // in time order (a file's packets come in order; this keeps it so if one does not)
    auto pos = std::upper_bound(list.begin(), list.end(), c.start, [](double t, const SubtitleCue& x) { return t < x.start; });
    list.insert(pos, std::move(c));
}

bool Subtitles::at(double t, SubtitleCue& out) const {
    std::lock_guard l(m_);
    if (selected_ < 0) return false;
    const auto it = cues_.find(selected_);
    if (it == cues_.end()) return false;
    const auto& list = it->second;
    // the last cue that started by t and has not ended (cues without an end last until the next, at most 10 s)
    auto up = std::upper_bound(list.begin(), list.end(), t, [](double v, const SubtitleCue& x) { return v < x.start; });
    std::vector<const SubtitleCue*> now;
    for (auto i = up; i != list.begin();) {
        --i;
        const double end = i->end > i->start ? i->end : std::min(i->start + 10.0, std::next(i) != list.end() ? std::next(i)->start : i->start + 10.0);
        if (t < end) now.push_back(&*i);
        if (t - i->start > 30) break;
    }
    if (now.empty()) return false;
    if (now.size() == 1) { out = *now.front(); return true; }
    // overlapping text cues are shown together (oldest on top)
    const SubtitleCue* best = now.back();
    out = *best;
    if (!best->text.empty()) {
        out.text.clear();
        uint64_t id = 0;
        for (auto i = now.rbegin(); i != now.rend(); ++i) {
            if ((*i)->text.empty()) continue;
            out.text += (out.text.empty() ? "" : "\n") + (*i)->text;
            id = id * 31 + (*i)->id;
        }
        out.id = id;
    }
    return true;
}

} // namespace veyamux
