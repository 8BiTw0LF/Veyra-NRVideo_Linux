// VeyAmux for Linux: PlayStation Remote Play (PS5 and PS4) as a source, built on chiaki-ng's protocol library
// (third_party/chiaki-ng, AGPL-3.0-only with OpenSSL exception).
//
// Capture path "ps:<console id>": the console's picture (H.264 / H.265, HDR as 10-bit PQ) is decoded here and handed to
// VeyAmux like a capture card's picture, so every feature works on it. Its sound is the audio source "@ps". The
// controller is passed to the console while the VeyAmux menu is closed (PlayerBridge -> setController()).
//
// Local network only: consoles are found by discovery (UDP broadcast), woken from rest mode and registered once with
// the 8-digit PIN from the console (Settings -> System -> Remote Play -> Link Device) and the PSN account ID.
// No Qt here (same as CaptureSource): the Qt side (PlayerBridge) keeps the registered consoles in QSettings.
#pragma once
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace veyamux { struct RawFrame; }

namespace veyamux::remoteplay {

struct Console {             // as found on the network
    std::string id;          // host id (MAC-like), stable
    std::string name;        // console nickname
    std::string host;        // IP address
    bool ps5 = false;
    bool ready = false;      // false: rest mode
    std::string runningApp;  // "" or the game's name
    std::string systemVersion;
};

struct Registered {          // what registration returns; stored by the caller
    std::string id, name, host;
    bool ps5 = false;
    int target = 0;          // ChiakiTarget
    std::string registKey;   // 16 bytes, hex
    std::string morning;     // 16 bytes (rp_key), hex
};

// ---- discovery (background; the list is refreshed every half second while it runs)
// Addresses typed in by hand (IPv4 or a host name): asked directly as well as by broadcast. For a console that the
// broadcast does not reach (another subnet, a VPN, Wi-Fi isolation) or whose answer a firewall drops (an answer to a
// broadcast is not seen as a reply by the connection tracking; one to a direct question is). Restarts a running search.
void setManualHosts(const std::vector<std::string>& hosts);
void startDiscovery();
void stopDiscovery();
std::vector<Console> consoles();

// ---- PSN account ID: the login page to open in a browser, and the account ID (base64) from the address the browser
// ends on after logging in (https://remoteplay.dl.playstation.net/remoteplay/redirect?code=...). Blocking (network).
std::string psnLoginUrl();
bool psnAccountId(const std::string& redirectUrl, std::string& accountIdBase64, std::string& error);

// ---- registration (blocking, up to ~30 s): `pin` = the 8 digits the console shows. A console that never answered the
// search can be registered by its address: id empty, ps5 as chosen; the id is then the console's MAC from the answer.
bool registerConsole(const Console& c, const std::string& accountIdBase64, uint32_t pin, Registered& out,
                     std::string& error);

// ---- wake from rest mode
bool wake(const Registered& r, std::string& error);

// ---- streaming
struct StreamConfig {
    Registered console;
    int height = 1080;       // 720 or 1080
    int fps = 60;            // 30 or 60
    bool hdr = false;        // PS5 only: H.265 HDR (10-bit PQ)
    std::string passcode;    // the console's 4-digit login passcode, if it has one
};
// The console the next "ps:<id>" capture connects to (set by the UI before startCapture).
void setStreamConfig(const StreamConfig& cfg);

class Stream {
public:
    Stream();
    ~Stream();
    bool open(const std::string& path, std::string& error);   // "ps:<id>"
    void close();
    enum class Result { Frame, Timeout, Error };
    Result readLatest(RawFrame& out, int timeoutMs, std::string& error);
    int width() const;
    int height() const;
    double fps() const;
    std::string name() const;
    struct Impl;
private:
    std::unique_ptr<Impl> p_;
};

// ---- while a stream runs (any thread)
// Controller state, PlayStation layout. buttons: bit mask of Button; sticks -1..1 (y down = +1); triggers 0..1.
enum Button : uint32_t {
    Cross = 1u << 0, Circle = 1u << 1, Square = 1u << 2, Triangle = 1u << 3,
    DpadLeft = 1u << 4, DpadRight = 1u << 5, DpadUp = 1u << 6, DpadDown = 1u << 7,
    L1 = 1u << 8, R1 = 1u << 9, L3 = 1u << 10, R3 = 1u << 11,
    Options = 1u << 12, Share = 1u << 13, Touchpad = 1u << 14, PS = 1u << 15,
};
void setController(uint32_t buttons, double lx, double ly, double rx, double ry, double l2, double r2);
// rumble from the game: low / high frequency motor 0..255
void setRumbleCallback(std::function<void(int low, int high)> cb);
bool active();
// for the frame-time overlay (F3): what can be measured of the delay between the console and VeyAmux
struct Stats {
    bool active = false;
    double rttMs = 0;        // round trip to the console, measured when the stream started (0: not yet)
    double decodeMs = 0;     // from a complete frame received to the decoded picture (average)
    double mbit = 0;         // video bitrate received, Mbit/s
    double loss = 0;         // packet loss 0..1
    uint64_t framesLost = 0; // frames the network lost since the start
    bool ps5 = false;
    bool hdr = false;
    bool hwDecode = false;   // decoded on the graphics card (VA-API)
};
Stats stats();
// Development check of the video decoder without a console: decodes an H.264 / H.265 elementary stream file the way a
// Remote Play stream is decoded (VA-API when available). veyamux-ps-decode-test.
struct DecodeTest { int frames = 0, w = 0, h = 0; bool hw = false, p010 = false; int firstLuma = 0; double ms = 0; std::string error; };
DecodeTest decodeTest(const std::string& file, bool hevc);
// 48 kHz interleaved stereo (the audio thread of the capture, like demo::audio)
void audio(int16_t* out, int frames);

} // namespace veyamux::remoteplay
