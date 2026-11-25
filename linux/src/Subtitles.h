// VeyAmux for Linux: subtitles for files and media server streams.
//
// The tracks in the file (text: SubRip, ASS/SSA, mov_text, WebVTT; pictures: PGS from Blu-ray, DVD, DVB) are decoded
// as the demux thread reads them (a second or two before they are due); files beside the video or from a server
// (.srt, .ass, .vtt, ...) are decoded whole when they are added. The window asks which cue is due now and draws it.
#pragma once
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct AVCodecContext;
struct AVFormatContext;
struct AVPacket;

namespace veyamux {

struct SubtitleTrack {
    int id = -1;             // the stream's index in the file, or 1000+ for an added file
    std::string lang;        // ISO 639 ("dan", "eng") or what the file name / server says
    std::string title;       // the track's name, if it has one
    std::string codec;
    bool bitmap = false;     // pictures (PGS, DVD)
    bool external = false;
    bool forced = false;
    bool isDefault = false;
};

struct SubtitleCue {
    double start = 0, end = 0;   // seconds on the video's clock
    std::string text;            // text subtitles (lines separated by \n, formatting codes removed)
    std::vector<uint8_t> rgba;   // picture subtitles: RGBA of the rectangle x, y, w, h on a frameW x frameH picture
    int x = 0, y = 0, w = 0, h = 0, frameW = 0, frameH = 0;
    uint64_t id = 0;             // changes when another cue is due
};

class Subtitles {
public:
    ~Subtitles();
    void reset();                                                // a new file: no tracks
    void addStream(AVFormatContext* fmt, int index, int videoW, int videoH);
    // a subtitle file (local path): decoded now; its track id, or -1 with the reason in error
    int addFile(const std::string& path, const std::string& lang, const std::string& title, std::string& error);
    std::vector<SubtitleTrack> tracks() const;
    bool select(int id);                                         // -1: off
    int selected() const;
    int selectedStream() const;                                  // the file's stream to decode, -1 when none
    void feed(const AVPacket* pkt);                              // demux thread
    void flush();                                                // after a seek
    bool at(double t, SubtitleCue& out) const;                   // the cue due at t

private:
    void decode(AVCodecContext* dec, const AVPacket* pkt, double timeBase, double startTime, std::vector<SubtitleCue>& list, int frameW,
                int frameH);

    mutable std::mutex m_;
    std::vector<SubtitleTrack> tracks_;
    std::map<int, std::vector<SubtitleCue>> cues_;
    std::map<int, double> timeBase_;
    AVFormatContext* fmt_ = nullptr;    // the open file (owned by MediaEngine)
    double startTime_ = 0;
    int videoW_ = 0, videoH_ = 0;
    AVCodecContext* dec_ = nullptr;     // the selected embedded track's decoder
    int selected_ = -1;
    int nextExternal_ = 1000;
    std::atomic<uint64_t> nextId_{1};
};

} // namespace veyamux
