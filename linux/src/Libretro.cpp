// VeyAmux for Linux: a minimal libretro frontend. See Libretro.h.
#include "Libretro.h"
#include "CaptureSource.h"   // RawFrame

#include "../third_party/libretro/libretro.h"

#include <dlfcn.h>
#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;

namespace veyamux::retro {

namespace {

using clk = std::chrono::steady_clock;

// ------------------------------------------------------------------ the core's functions
struct Core {
    void* so = nullptr;
    void (*init)() = nullptr;
    void (*deinit)() = nullptr;
    unsigned (*apiVersion)() = nullptr;
    void (*getSystemInfo)(retro_system_info*) = nullptr;
    void (*getAvInfo)(retro_system_av_info*) = nullptr;
    void (*setEnvironment)(retro_environment_t) = nullptr;
    void (*setVideoRefresh)(retro_video_refresh_t) = nullptr;
    void (*setAudioSample)(retro_audio_sample_t) = nullptr;
    void (*setAudioSampleBatch)(retro_audio_sample_batch_t) = nullptr;
    void (*setInputPoll)(retro_input_poll_t) = nullptr;
    void (*setInputState)(retro_input_state_t) = nullptr;
    void (*setControllerPortDevice)(unsigned, unsigned) = nullptr;
    void (*reset)() = nullptr;
    void (*run)() = nullptr;
    size_t (*serializeSize)() = nullptr;
    bool (*serialize)(void*, size_t) = nullptr;
    bool (*unserialize)(const void*, size_t) = nullptr;
    bool (*loadGame)(const retro_game_info*) = nullptr;
    void (*unloadGame)() = nullptr;
    void* (*getMemoryData)(unsigned) = nullptr;
    size_t (*getMemorySize)(unsigned) = nullptr;

    bool load(const std::string& path, std::string& error) {
        so = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!so) { error = std::string("Could not load the emulator core: ") + dlerror(); return false; }
        bool ok = true;
        auto sym = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(so, name));
            if (!fn) { ok = false; if (error.empty()) error = std::string("The emulator core has no ") + name; }
        };
        sym(init, "retro_init"); sym(deinit, "retro_deinit"); sym(apiVersion, "retro_api_version");
        sym(getSystemInfo, "retro_get_system_info"); sym(getAvInfo, "retro_get_system_av_info");
        sym(setEnvironment, "retro_set_environment"); sym(setVideoRefresh, "retro_set_video_refresh");
        sym(setAudioSample, "retro_set_audio_sample"); sym(setAudioSampleBatch, "retro_set_audio_sample_batch");
        sym(setInputPoll, "retro_set_input_poll"); sym(setInputState, "retro_set_input_state");
        sym(setControllerPortDevice, "retro_set_controller_port_device");
        sym(reset, "retro_reset"); sym(run, "retro_run");
        sym(serializeSize, "retro_serialize_size"); sym(serialize, "retro_serialize"); sym(unserialize, "retro_unserialize");
        sym(loadGame, "retro_load_game"); sym(unloadGame, "retro_unload_game");
        sym(getMemoryData, "retro_get_memory_data"); sym(getMemorySize, "retro_get_memory_size");
        if (ok && apiVersion() != RETRO_API_VERSION) { ok = false; error = "The emulator core uses another libretro version"; }
        if (!ok) { dlclose(so); so = nullptr; }
        return ok;
    }
    void unload() { if (so) dlclose(so); so = nullptr; }
};

// ------------------------------------------------------------------ shared state (libretro has no user pointer)
std::mutex gameM;
Game current;

std::atomic<uint32_t> padButtons[4]{}, keyButtons{0};    // players 1-4; the keyboard is player 1
std::atomic<int16_t> axes[4][4]{};                         // per player: left x, left y, right x, right y
// what the core reads: the above taken once per frame on the core thread (latchInput), so a press that comes while a
// frame runs, or between the real frame and the frames ahead of it, is seen from the next frame on and never half
uint32_t latchedButtons[4]{};
int16_t latchedAxes[4][4]{};
void latchInput() {
    for (int i = 0; i < 4; ++i) {
        latchedButtons[i] = padButtons[i].load() | (i == 0 ? keyButtons.load() : 0u);
        for (int k = 0; k < 4; ++k) latchedAxes[i][k] = axes[i][k].load();
    }
}
std::atomic<int> runAheadWanted{0};
std::atomic<bool> rewindWanted{false}, rewindHeld{false};
bool suppressVideo = false, suppressAudio = false;        // core thread: frames run only for run-ahead
std::atomic<bool> stickDpad{true};
std::mutex rumbleM;
std::function<void(int, int)> rumbleCb;
int rumbleStrong = 0, rumbleWeak = 0;

std::mutex msgM;
std::string message;
void say(const std::string& m) { std::lock_guard l(msgM); message = m; }

// the thread that runs the core, and what it shares with the readers
struct Runner {
    std::thread thread;
    std::atomic<bool> quit{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> running{false};
    // load result
    std::mutex loadM;
    std::condition_variable loadCv;
    int loadState = 0;               // 0 loading, 1 running, -1 failed
    std::string loadError;
    // the newest picture
    std::mutex frameM;
    std::condition_variable frameCv;
    std::vector<uint8_t> frame;      // RGBA
    int fw = 0, fh = 0;
    uint64_t frameSeq = 0;
    clk::time_point frameAt{};
    // sound at the core's rate, interleaved stereo
    std::mutex audioM;
    std::deque<int16_t> sound;
    double srcRate = 48000;
    double resamplePos = 0;          // fractional position between the first two frames in `sound`
    // what is running
    std::mutex infoM;
    Info info;
    // commands for the core thread
    std::mutex cmdM;
    std::vector<std::function<void()>> cmds;
};
Runner* runner = nullptr;                  // the running one, for the core's callbacks (on its own thread)
std::shared_ptr<Runner> active;           // the same, kept alive for readers that are still using it
std::mutex runnerM;
std::shared_ptr<Runner> get() { std::lock_guard l(runnerM); return active; }

// core-thread-only state
retro_pixel_format pixFmt = RETRO_PIXEL_FORMAT_0RGB1555;
unsigned rotation = 0;
retro_game_geometry geometry{};
double fpsNow = 60;
std::map<std::string, std::string> options;   // core option -> value
std::string optionValue;                      // returned by GET_VARIABLE (must stay valid)
std::string sysDirC, saveDirC, coreDirC, libPathC;
bool shutdownAsked = false;
std::vector<uint32_t> rowRgba;
// discs (a game on several CDs, opened as a .m3u playlist): the core's disc tray
retro_set_eject_state_t diskEject = nullptr;
retro_get_image_index_t diskGetIndex = nullptr;
retro_set_image_index_t diskSetIndex = nullptr;
retro_get_num_images_t diskCount = nullptr;
int diskInsertIn = 0;            // frames until the tray closes again after a change
std::atomic<int> disksNow{0}, diskNow{0};                // one source row as RGBA
std::vector<int> xmap, ymap;

// ------------------------------------------------------------------ core callbacks
void RETRO_CALLCONV logCb(retro_log_level level, const char* fmt, ...) {
    if (level < RETRO_LOG_INFO) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    size_t n = std::strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    std::fprintf(stderr, "[core] %s\n", buf);
}

bool RETRO_CALLCONV rumbleSet(unsigned port, retro_rumble_effect effect, uint16_t strength) {
    if (port != 0) return true;
    std::lock_guard l(rumbleM);
    (effect == RETRO_RUMBLE_STRONG ? rumbleStrong : rumbleWeak) = strength >> 8;
    if (rumbleCb) rumbleCb(rumbleStrong, rumbleWeak);
    return true;
}

void setOptionDefault(const char* key, const char* value) {
    if (!key || !value) return;
    if (!options.count(key)) options[key] = value;
}

bool RETRO_CALLCONV environment(unsigned cmd, void* data) {
    // a core may pass no data to say "none" (GPGX does for the disc control of a cartridge): only the commands
    // that handle that may go on
    if (!data && cmd != RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE && cmd != RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE
        && cmd != RETRO_ENVIRONMENT_SHUTDOWN && cmd != RETRO_ENVIRONMENT_GET_INPUT_BITMASKS && cmd != RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME
        && cmd != RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS)
        return false;
    switch (cmd) {
    case RETRO_ENVIRONMENT_SET_ROTATION: rotation = *static_cast<const unsigned*>(data) & 3; return true;
    case RETRO_ENVIRONMENT_GET_OVERSCAN: *static_cast<bool*>(data) = false; return true;
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: *static_cast<bool*>(data) = true; return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE:
        if (auto* m = static_cast<const retro_message*>(data); m && m->msg) std::fprintf(stderr, "[core] message: %s\n", m->msg);
        return true;
    case RETRO_ENVIRONMENT_SET_MESSAGE_EXT:
        if (auto* m = static_cast<const retro_message_ext*>(data); m && m->msg) std::fprintf(stderr, "[core] message: %s\n", m->msg);
        return true;
    case RETRO_ENVIRONMENT_GET_MESSAGE_INTERFACE_VERSION: *static_cast<unsigned*>(data) = 1; return true;
    case RETRO_ENVIRONMENT_SHUTDOWN: shutdownAsked = true; return true;
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL: return true;
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *static_cast<const char**>(data) = sysDirC.c_str(); return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *static_cast<const char**>(data) = saveDirC.c_str(); return true;
    case RETRO_ENVIRONMENT_GET_CORE_ASSETS_DIRECTORY: *static_cast<const char**>(data) = sysDirC.c_str(); return true;
    case RETRO_ENVIRONMENT_GET_LIBRETRO_PATH: *static_cast<const char**>(data) = libPathC.c_str(); return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
        const auto f = *static_cast<const retro_pixel_format*>(data);
        if (f != RETRO_PIXEL_FORMAT_0RGB1555 && f != RETRO_PIXEL_FORMAT_XRGB8888 && f != RETRO_PIXEL_FORMAT_RGB565) return false;
        pixFmt = f;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO: case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME: case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
    case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS: case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
    case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE: case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
        return true;
    case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION: *static_cast<unsigned*>(data) = 1; return true;
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE: {
        const auto* d = static_cast<const retro_disk_control_callback*>(data);
        if (!d) { diskEject = nullptr; diskGetIndex = nullptr; diskSetIndex = nullptr; diskCount = nullptr; return true; }   // no discs (a cartridge)
        diskEject = d->set_eject_state; diskGetIndex = d->get_image_index; diskSetIndex = d->set_image_index; diskCount = d->get_num_images;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE: {
        const auto* d = static_cast<const retro_disk_control_ext_callback*>(data);
        if (!d) { diskEject = nullptr; diskGetIndex = nullptr; diskSetIndex = nullptr; diskCount = nullptr; return true; }   // no discs (a cartridge)
        diskEject = d->set_eject_state; diskGetIndex = d->get_image_index; diskSetIndex = d->set_image_index; diskCount = d->get_num_images;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_HW_RENDER: case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
        return false;   // software cores only
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS: return true;
    case RETRO_ENVIRONMENT_GET_INPUT_MAX_USERS: *static_cast<unsigned*>(data) = 4; return true;
    case RETRO_ENVIRONMENT_GET_INPUT_DEVICE_CAPABILITIES:
        *static_cast<uint64_t*>(data) = (1 << RETRO_DEVICE_JOYPAD) | (1 << RETRO_DEVICE_ANALOG); return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: static_cast<retro_log_callback*>(data)->log = logCb; return true;
    case RETRO_ENVIRONMENT_GET_RUMBLE_INTERFACE: static_cast<retro_rumble_interface*>(data)->set_rumble_state = rumbleSet; return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE: *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH; return true;
    case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *static_cast<int*>(data) = 3; return true;
    case RETRO_ENVIRONMENT_GET_FASTFORWARDING: *static_cast<bool*>(data) = false; return true;
    case RETRO_ENVIRONMENT_GET_TARGET_REFRESH_RATE: *static_cast<float*>(data) = float(fpsNow); return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *static_cast<bool*>(data) = false; return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *static_cast<unsigned*>(data) = 2; return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
        // "Description; default|other|..."
        for (auto* v = static_cast<const retro_variable*>(data); v && v->key; ++v) {
            if (!v->value) continue;
            std::string s = v->value;
            if (auto p = s.find("; "); p != std::string::npos) s = s.substr(p + 2);
            if (auto p = s.find('|'); p != std::string::npos) s = s.substr(0, p);
            setOptionDefault(v->key, s.c_str());
        }
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        for (auto* d = static_cast<const retro_core_option_definition*>(data); d && d->key; ++d)
            setOptionDefault(d->key, d->default_value ? d->default_value : d->values[0].value);
        return true;
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL: {
        auto* intl = static_cast<const retro_core_options_intl*>(data);
        for (auto* d = intl ? intl->us : nullptr; d && d->key; ++d)
            setOptionDefault(d->key, d->default_value ? d->default_value : d->values[0].value);
        return true;
    }
    case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2: case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL: {
        const retro_core_options_v2* o = cmd == RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2
            ? static_cast<const retro_core_options_v2*>(data)
            : (data ? static_cast<const retro_core_options_v2_intl*>(data)->us : nullptr);
        for (auto* d = o ? o->definitions : nullptr; d && d->key; ++d)
            setOptionDefault(d->key, d->default_value ? d->default_value : d->values[0].value);
        return true;
    }
    case RETRO_ENVIRONMENT_GET_VARIABLE: {
        auto* v = static_cast<retro_variable*>(data);
        if (!v || !v->key) return false;
        auto it = options.find(v->key);
        if (it == options.end()) { v->value = nullptr; return false; }
        optionValue = it->second;
        v->value = optionValue.c_str();
        return true;
    }
    case RETRO_ENVIRONMENT_SET_VARIABLE: {
        auto* v = static_cast<const retro_variable*>(data);
        if (v && v->key && v->value) options[v->key] = v->value;
        return true;
    }
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        geometry = *static_cast<const retro_game_geometry*>(data);
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
        const auto* av = static_cast<const retro_system_av_info*>(data);
        geometry = av->geometry;
        fpsNow = av->timing.fps > 1 ? av->timing.fps : 60;
        if (runner) {
            std::lock_guard l(runner->audioM);
            runner->srcRate = av->timing.sample_rate > 1000 ? av->timing.sample_rate : 48000;
            runner->sound.clear();
        }
        return true;
    }
    default:
        return false;
    }
}

// the picture: converted to RGBA, rotated, the pixels scaled up by a whole number and stretched to the aspect ratio
void RETRO_CALLCONV videoRefresh(const void* data, unsigned w, unsigned h, size_t pitch) {
    Runner* r = runner;
    if (!r || !data || w == 0 || h == 0 || suppressVideo) return;   // a repeated picture (CAN_DUPE): the last one stays
    const bool turned = rotation == 1 || rotation == 3;
    const int dw0 = int(turned ? h : w), dh0 = int(turned ? w : h);
    float aspect = geometry.aspect_ratio > 0 ? geometry.aspect_ratio : float(w) / float(h);
    if (turned) aspect = 1.0f / aspect;
    const int k = std::max(1, 1080 / dh0);
    int oh = (dh0 * k) & ~1;
    int ow = int(std::lround(oh * aspect)) & ~1;
    if (ow < 2 || oh < 2) return;
    if (int(xmap.size()) != ow) xmap.resize(size_t(ow));
    if (int(ymap.size()) != oh) ymap.resize(size_t(oh));
    for (int x = 0; x < ow; ++x) xmap[size_t(x)] = std::min(dw0 - 1, int((int64_t(x) * dw0) / ow));
    for (int y = 0; y < oh; ++y) ymap[size_t(y)] = std::min(dh0 - 1, int((int64_t(y) * dh0) / oh));

    auto px = [&](unsigned sx, unsigned sy) -> uint32_t {   // source pixel as RGBA bytes (little endian)
        const auto* row = static_cast<const uint8_t*>(data) + size_t(sy) * pitch;
        if (pixFmt == RETRO_PIXEL_FORMAT_XRGB8888) {
            const uint32_t p = reinterpret_cast<const uint32_t*>(row)[sx];
            return 0xff000000u | ((p & 0xffu) << 16) | (p & 0xff00u) | ((p >> 16) & 0xffu);
        }
        const uint16_t p = reinterpret_cast<const uint16_t*>(row)[sx];
        uint32_t R, G, B;
        if (pixFmt == RETRO_PIXEL_FORMAT_RGB565) {
            R = (p >> 11) & 31; G = (p >> 5) & 63; B = p & 31;
            R = (R << 3) | (R >> 2); G = (G << 2) | (G >> 4); B = (B << 3) | (B >> 2);
        } else {
            R = (p >> 10) & 31; G = (p >> 5) & 31; B = p & 31;
            R = (R << 3) | (R >> 2); G = (G << 3) | (G >> 2); B = (B << 3) | (B >> 2);
        }
        return 0xff000000u | (B << 16) | (G << 8) | R;
    };

    std::unique_lock l(r->frameM);
    r->frame.resize(size_t(ow) * oh * 4);
    auto* out = reinterpret_cast<uint32_t*>(r->frame.data());
    rowRgba.resize(size_t(dw0));
    int lastDy = -1;
    for (int y = 0; y < oh; ++y) {
        const int dy = ymap[size_t(y)];
        if (dy != lastDy) {   // the displayed row dy, converted once and reused for the rows it is scaled to
            for (int dx = 0; dx < dw0; ++dx) {
                unsigned sx, sy;
                switch (rotation) {
                case 1: sx = w - 1 - unsigned(dy); sy = unsigned(dx); break;          // 90 degrees counter-clockwise
                case 2: sx = w - 1 - unsigned(dx); sy = h - 1 - unsigned(dy); break;
                case 3: sx = unsigned(dy); sy = h - 1 - unsigned(dx); break;
                default: sx = unsigned(dx); sy = unsigned(dy);
                }
                rowRgba[size_t(dx)] = px(sx, sy);
            }
            lastDy = dy;
        }
        uint32_t* o = out + size_t(y) * ow;
        for (int x = 0; x < ow; ++x) o[x] = rowRgba[size_t(xmap[size_t(x)])];
    }
    r->fw = ow; r->fh = oh;
    r->frameAt = clk::now();
    ++r->frameSeq;
    l.unlock();
    r->frameCv.notify_all();
    {
        std::lock_guard il(r->infoM);
        r->info.w = int(w); r->info.h = int(h); r->info.aspect = aspect;
    }
}

void pushSound(const int16_t* d, size_t frames) {
    Runner* r = runner;
    if (!r || suppressAudio) return;
    std::lock_guard l(r->audioM);
    r->sound.insert(r->sound.end(), d, d + frames * 2);
    const size_t cap = size_t(r->srcRate * 0.5) * 2;   // more than half a second queued: the oldest goes
    if (r->sound.size() > cap) r->sound.erase(r->sound.begin(), r->sound.begin() + std::ptrdiff_t(r->sound.size() - cap));
}
void RETRO_CALLCONV audioSample(int16_t left, int16_t right) { const int16_t s[2]{left, right}; pushSound(s, 1); }
size_t RETRO_CALLCONV audioBatch(const int16_t* data, size_t frames) { pushSound(data, frames); return frames; }

void RETRO_CALLCONV inputPoll() {}

int16_t RETRO_CALLCONV inputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port > 3) return 0;
    uint32_t b = latchedButtons[port];
    if (stickDpad.load()) {   // the left stick as the D-pad
        const int lx = latchedAxes[port][0], ly = latchedAxes[port][1];
        if (lx < -16000) b |= 1u << RETRO_DEVICE_ID_JOYPAD_LEFT;
        if (lx > 16000) b |= 1u << RETRO_DEVICE_ID_JOYPAD_RIGHT;
        if (ly < -16000) b |= 1u << RETRO_DEVICE_ID_JOYPAD_UP;
        if (ly > 16000) b |= 1u << RETRO_DEVICE_ID_JOYPAD_DOWN;
    }
    if (device == RETRO_DEVICE_JOYPAD) {
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return int16_t(b & 0xffff);
        return id < 16 ? int16_t((b >> id) & 1) : 0;
    }
    if (device == RETRO_DEVICE_ANALOG) {
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON)
            return id < 16 && ((b >> id) & 1) ? 0x7fff : 0;
        if (index <= RETRO_DEVICE_INDEX_ANALOG_RIGHT && id <= RETRO_DEVICE_ID_ANALOG_Y) return latchedAxes[port][index * 2 + id];
    }
    return 0;
}

// ------------------------------------------------------------------ files
bool readFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
bool writeFile(const std::string& path, const void* d, size_t n) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(static_cast<const char*>(d), std::streamsize(n));
        if (!f) return false;
    }
    fs::rename(tmp, path, ec);
    return !ec;
}
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c))); return s; }
std::string extOf(const std::string& p) {
    const auto n = fs::path(p).extension().string();
    return n.empty() ? n : lower(n.substr(1));
}
bool extListed(const std::string& list, const std::string& ext) {
    size_t s = 0;
    while (s <= list.size()) {
        const size_t e = std::min(list.find('|', s), list.size());
        if (lower(list.substr(s, e - s)) == ext) return true;
        s = e + 1;
    }
    return false;
}
std::string baseName(const std::string& p) { return fs::path(p).stem().string(); }

// ------------------------------------------------------------------ the core thread
void coreThread(Runner* r, Game g) {
    Core core;
    std::string err;
    auto fail = [&](const std::string& e) {
        std::lock_guard l(r->loadM);
        r->loadState = -1;
        r->loadError = e;
        r->loadCv.notify_all();
    };
    pixFmt = RETRO_PIXEL_FORMAT_0RGB1555;
    rotation = 0;
    geometry = {};
    options.clear();
    shutdownAsked = false;
    diskEject = nullptr; diskGetIndex = nullptr; diskSetIndex = nullptr; diskCount = nullptr; diskInsertIn = 0;
    disksNow = 0; diskNow = 0;
    sysDirC = g.systemDir; saveDirC = g.saveDir; libPathC = g.core;
    { std::error_code ec; fs::create_directories(g.systemDir, ec); fs::create_directories(g.saveDir, ec); fs::create_directories(g.stateDir, ec); }
    if (!core.load(g.core, err)) { fail(err); return; }
    // RetroArch's order: the environment, retro_init, then the other callbacks (Mesen only creates what its
    // set_video_refresh writes to in retro_init)
    core.setEnvironment(environment);
    core.init();
    core.setVideoRefresh(videoRefresh);
    core.setAudioSample(audioSample);
    core.setAudioSampleBatch(audioBatch);
    core.setInputPoll(inputPoll);
    core.setInputState(inputState);
    retro_system_info si{};
    core.getSystemInfo(&si);
    const std::string coreName = std::string(si.library_name ? si.library_name : "core") + " " + (si.library_version ? si.library_version : "");
    const std::string exts = si.valid_extensions ? si.valid_extensions : "";
    const bool fullPath = si.need_fullpath;

    // the game: a .zip the core cannot read itself is unpacked (into memory, or a temporary file for cores that read
    // the file themselves)
    std::string romPath = g.rom, tmpFile;
    std::vector<uint8_t> romData;
    if (isArchive(g.rom) && !extListed(exts, extOf(g.rom))) {
        const auto entries = zipList(g.rom);
        const ZipEntry* pick = nullptr;
        // (a core that names no file types: the largest file)
        for (const auto& e : entries)
            if ((exts.empty() || extListed(exts, extOf(e.name))) && (!pick || e.size > pick->size)) pick = &e;
        if (!pick) { core.deinit(); core.unload(); fail("The zip file holds no game this emulator can run"); return; }
        if (!zipRead(g.rom, pick->name, romData, err)) { core.deinit(); core.unload(); fail(err); return; }
        romPath = (fs::path(g.rom).parent_path() / fs::path(pick->name).filename()).string();
        if (fullPath) {
            const char* rt = std::getenv("XDG_RUNTIME_DIR");
            const fs::path dir = fs::path(rt && *rt ? rt : "/tmp") / "veyamux-retro";
            std::error_code ec;
            fs::create_directories(dir, ec);
            tmpFile = (dir / fs::path(pick->name).filename()).string();
            if (!writeFile(tmpFile, romData.data(), romData.size())) { core.deinit(); core.unload(); fail("Could not unpack the game"); return; }
            romPath = tmpFile;
            romData.clear();
        }
    } else if (!fullPath && !readFile(g.rom, romData)) {
        core.deinit(); core.unload(); fail("Could not read " + g.rom); return;
    }
    retro_game_info gi{};
    gi.path = romPath.c_str();
    gi.data = fullPath ? nullptr : romData.data();
    gi.size = fullPath ? 0 : romData.size();
    if (!core.loadGame(&gi)) {
        core.deinit(); core.unload();
        if (!tmpFile.empty()) { std::error_code ec; fs::remove(tmpFile, ec); }
        fail("The emulator could not start the game (a missing BIOS file? See the BIOS folder)");
        return;
    }
    for (unsigned port = 0; port < 4; ++port) core.setControllerPortDevice(port, RETRO_DEVICE_JOYPAD);
    retro_system_av_info av{};
    core.getAvInfo(&av);
    geometry = av.geometry;
    fpsNow = av.timing.fps > 1 ? av.timing.fps : 60;
    {
        std::lock_guard l(r->audioM);
        r->srcRate = av.timing.sample_rate > 1000 ? av.timing.sample_rate : 48000;
    }
    {
        std::lock_guard l(r->infoM);
        r->info.core = coreName;
        r->info.fps = fpsNow;
        r->info.sampleRate = av.timing.sample_rate;
        r->info.w = int(av.geometry.base_width); r->info.h = int(av.geometry.base_height);
        r->info.aspect = av.geometry.aspect_ratio;
    }
    // the battery save
    const std::string srm = (fs::path(g.saveDir) / (baseName(g.rom) + ".srm")).string();
    std::vector<uint8_t> lastSave;
    if (void* mem = core.getMemoryData(RETRO_MEMORY_SAVE_RAM); mem && core.getMemorySize(RETRO_MEMORY_SAVE_RAM)) {
        const size_t n = core.getMemorySize(RETRO_MEMORY_SAVE_RAM);
        std::vector<uint8_t> d;
        if (readFile(srm, d) && !d.empty()) std::memcpy(mem, d.data(), std::min(n, d.size()));
        lastSave.assign(static_cast<uint8_t*>(mem), static_cast<uint8_t*>(mem) + n);
    }
    auto saveBattery = [&] {
        void* mem = core.getMemoryData(RETRO_MEMORY_SAVE_RAM);
        const size_t n = mem ? core.getMemorySize(RETRO_MEMORY_SAVE_RAM) : 0;
        if (!n) return;
        if (lastSave.size() == n && std::memcmp(lastSave.data(), mem, n) == 0) return;
        if (writeFile(srm, mem, n)) lastSave.assign(static_cast<uint8_t*>(mem), static_cast<uint8_t*>(mem) + n);
    };
    std::fprintf(stderr, "Games: %s runs %s (%.3f fps, %.0f Hz sound, %ux%u)\n", coreName.c_str(), g.rom.c_str(),
                 fpsNow, av.timing.sample_rate, av.geometry.base_width, av.geometry.base_height);
    r->running = true;
    {
        std::lock_guard l(r->loadM);
        r->loadState = 1;
        r->loadCv.notify_all();
    }
    // states: "Continue" (the automatic state of the last quit), run-ahead and rewind all go through retro_serialize
    const std::string autoState = autoStatePath(g.stateDir, g.rom);
    std::vector<uint8_t> state;
    auto serialize = [&](std::vector<uint8_t>& out) {
        const size_t n = core.serializeSize();
        if (!n) return false;
        out.resize(n);
        return core.serialize(out.data(), n);
    };
    bool resumePending = g.resume;
    auto tryResume = [&] {
        std::vector<uint8_t> d;
        if (!readFile(autoState, d) || d.empty()) { resumePending = false; return; }
        if (core.unserialize(d.data(), d.size())) { resumePending = false; say("Continued where you left off"); }
    };
    if (resumePending) tryResume();   // (some emulators only take a state after the first frame: tried again then)
    runAheadWanted = g.runAhead;
    rewindWanted = g.rewind;
    std::deque<std::vector<uint8_t>> rewindBuf;
    uint64_t frameNo = 0;
    bool runAheadBroken = false;
    auto frame = [&] {
        ++frameNo;
        latchInput();
        // rewind: one kept state back per frame while held (shown, without its sound)
        if (rewindHeld.load() && rewindWanted.load()) {
            if (rewindBuf.empty()) return;
            const auto& st = rewindBuf.back();
            core.unserialize(st.data(), st.size());
            rewindBuf.pop_back();
            suppressAudio = true;
            core.run();
            suppressAudio = false;
            return;
        }
        if (rewindWanted.load()) {
            // about 20 s, at most 256 MB: a big state (PlayStation) is kept every few frames
            const size_t n = core.serializeSize();
            const int every = n > (1u << 20) ? 3 : 1;
            const size_t keep = n ? std::min<size_t>(size_t(fpsNow * 20) / every, (256u << 20) / n) : 0;
            if (keep && frameNo % every == 0) {
                std::vector<uint8_t> st;
                if (rewindBuf.size() >= keep) { st = std::move(rewindBuf.front()); rewindBuf.pop_front(); }
                if (serialize(st)) rewindBuf.push_back(std::move(st));
            }
        } else if (!rewindBuf.empty()) {
            rewindBuf.clear();
        }
        const int ahead = runAheadBroken ? 0 : runAheadWanted.load();
        if (ahead <= 0) { core.run(); return; }
        // run-ahead: the real frame (its sound kept, its picture not), a state, the frames ahead (only the last one's
        // picture shown, no sound), and back to the state. The picture is then what the game shows `ahead` frames
        // after the input, so its own delay is gone.
        suppressVideo = true;
        core.run();
        suppressVideo = false;
        if (!serialize(state)) {
            runAheadBroken = true;
            say("Run-ahead does not work with this emulator");
            return;
        }
        suppressAudio = true;
        for (int i = 0; i < ahead; ++i) {
            suppressVideo = i + 1 < ahead;
            core.run();
        }
        suppressVideo = false;
        suppressAudio = false;
        core.unserialize(state.data(), state.size());
    };

    auto next = clk::now();
    auto lastSaveCheck = clk::now();
    while (!r->quit.load() && !shutdownAsked) {
        // commands from the UI
        std::vector<std::function<void()>> cmds;
        { std::lock_guard l(r->cmdM); cmds.swap(r->cmds); }
        for (auto& c : cmds) c();
        if (r->paused.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); next = clk::now(); continue; }
        frame();
        if (resumePending && frameNo >= 1) tryResume(), resumePending = false;
        if (diskCount) { disksNow = int(diskCount()); if (diskGetIndex) diskNow = int(diskGetIndex()); }
        if (diskInsertIn > 0 && --diskInsertIn == 0 && diskEject) diskEject(false);   // the new disc is in: tray closed
        const auto period = std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(1.0 / fpsNow));
        next += period;
        const auto now = clk::now();
        if (next < now - period * 4) next = now;   // fell behind (a slow core, a stall): no catching up in a burst
        else std::this_thread::sleep_until(next);
        if (now - lastSaveCheck > std::chrono::seconds(10)) { saveBattery(); lastSaveCheck = now; }
    }
    saveBattery();
    // the automatic state for "Continue" (not when the game shut itself down)
    if (!shutdownAsked && frameNo > 30) {
        std::vector<uint8_t> st;
        if (serialize(st)) writeFile(autoState, st.data(), st.size());
    }
    r->running = false;
    core.unloadGame();
    core.deinit();
    core.unload();
    if (!tmpFile.empty()) { std::error_code ec; fs::remove(tmpFile, ec); }
    // commands that came too late
    std::lock_guard l(r->cmdM);
    r->cmds.clear();
}

// runs on the core thread between frames (the core's serialize functions are reached through dlsym again: the
// command closures have no Core; RTLD_NOLOAD finds the already loaded library)
template <class Fn> void post(Fn&& fn) {
    auto r = get();
    if (!r || !r->running.load()) return;
    std::lock_guard c(r->cmdM);
    r->cmds.emplace_back(std::forward<Fn>(fn));
}

void* coreSym(const char* name) {
    std::string path;
    { std::lock_guard l(gameM); path = current.core; }
    void* so = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (!so) return nullptr;
    void* s = dlsym(so, name);
    dlclose(so);   // drops the reference RTLD_NOLOAD added
    return s;
}

std::string statePathFor(int slot) {
    std::lock_guard l(gameM);
    return (fs::path(current.stateDir) / (baseName(current.rom) + ".state" + std::to_string(slot))).string();
}

} // namespace

// ------------------------------------------------------------------ public

void setGame(const Game& g) { std::lock_guard l(gameM); current = g; stickDpad = g.stickAsDpad; }
Game game() { std::lock_guard l(gameM); return current; }

void setPad(int port, uint32_t buttons, double lx, double ly, double rx, double ry) {
    if (port < 0 || port > 3) return;
    padButtons[port] = buttons;
    auto ax = [](double v) { return int16_t(std::clamp(std::lround(v * 32767.0), -32768L, 32767L)); };
    axes[port][0] = ax(lx); axes[port][1] = ax(ly); axes[port][2] = ax(rx); axes[port][3] = ax(ry);
}
void setPad(uint32_t buttons, double lx, double ly, double rx, double ry) { setPad(0, buttons, lx, ly, rx, ry); }
void setRunAhead(int frames) { runAheadWanted = std::clamp(frames, 0, 2); }
void setRewind(bool on) { rewindWanted = on; }
void setRewinding(bool held) { rewindHeld = held; }
std::string autoStatePath(const std::string& stateDir, const std::string& rom) {
    return (fs::path(stateDir) / (baseName(rom) + ".auto.state")).string();
}
void setKeys(uint32_t buttons) { keyButtons = buttons; }

uint32_t fromPlayStation(uint32_t ps, double l2, double r2) {
    // remoteplay::Button: Cross 0, Circle 1, Square 2, Triangle 3, Left 4, Right 5, Up 6, Down 7, L1 8, R1 9, L3 10,
    // R3 11, Options 12, Share 13
    static const int map[][2] = {
        {0, RETRO_DEVICE_ID_JOYPAD_B}, {1, RETRO_DEVICE_ID_JOYPAD_A}, {2, RETRO_DEVICE_ID_JOYPAD_Y}, {3, RETRO_DEVICE_ID_JOYPAD_X},
        {4, RETRO_DEVICE_ID_JOYPAD_LEFT}, {5, RETRO_DEVICE_ID_JOYPAD_RIGHT}, {6, RETRO_DEVICE_ID_JOYPAD_UP}, {7, RETRO_DEVICE_ID_JOYPAD_DOWN},
        {8, RETRO_DEVICE_ID_JOYPAD_L}, {9, RETRO_DEVICE_ID_JOYPAD_R}, {10, RETRO_DEVICE_ID_JOYPAD_L3}, {11, RETRO_DEVICE_ID_JOYPAD_R3},
        {12, RETRO_DEVICE_ID_JOYPAD_START}, {13, RETRO_DEVICE_ID_JOYPAD_SELECT}, {14, RETRO_DEVICE_ID_JOYPAD_SELECT}};
    uint32_t b = 0;
    for (const auto& m : map) if (ps & (1u << m[0])) b |= 1u << m[1];
    if (l2 > 0.5) b |= 1u << RETRO_DEVICE_ID_JOYPAD_L2;
    if (r2 > 0.5) b |= 1u << RETRO_DEVICE_ID_JOYPAD_R2;
    return b;
}

void setRumbleCallback(std::function<void(int, int)> cb) { std::lock_guard l(rumbleM); rumbleCb = std::move(cb); }

void audio(int16_t* out, int frames) {
    auto r = get();
    if (!r) { std::memset(out, 0, size_t(frames) * 4); std::this_thread::sleep_for(std::chrono::milliseconds(frames / 48)); return; }
    // paced by the playback stream: wait until the core has made enough sound for this block, then resample. The
    // rate follows how full the queue is (a fraction of a percent), so the core's clock and the sound card's never
    // drift apart (no growing delay, no gaps).
    const auto until = clk::now() + std::chrono::milliseconds(frames / 48 + 30);
    for (;;) {
        {
            std::lock_guard l(r->audioM);
            const double ratio = r->srcRate / 48000.0;
            if (r->sound.size() / 2 >= size_t(frames * ratio) + 2 || r->quit.load()) break;
        }
        if (clk::now() > until) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::lock_guard l(r->audioM);
    const double target = r->srcRate * 0.05;                // 50 ms queued
    const double fill = double(r->sound.size() / 2);
    const double adjust = std::clamp((fill - target) / target * 0.005, -0.005, 0.005);
    const double step = r->srcRate / 48000.0 * (1.0 + adjust);
    double pos = r->resamplePos;
    size_t avail = r->sound.size() / 2;
    int i = 0;
    for (; i < frames; ++i) {
        const size_t a = size_t(pos);
        if (a + 1 >= avail) break;
        const double t = pos - double(a);
        for (int c = 0; c < 2; ++c) {
            const double v = r->sound[a * 2 + size_t(c)] * (1.0 - t) + r->sound[(a + 1) * 2 + size_t(c)] * t;
            out[i * 2 + c] = int16_t(std::lround(v));
        }
        pos += step;
    }
    for (int j = i; j < frames; ++j) out[j * 2] = out[j * 2 + 1] = 0;   // the core is behind: silence
    const size_t used = std::min(size_t(pos), avail);
    r->sound.erase(r->sound.begin(), r->sound.begin() + std::ptrdiff_t(used * 2));
    r->resamplePos = pos - double(used);
}

bool running() { auto r = get(); return r && r->running.load(); }

void saveState(int slot) {
    const std::string path = statePathFor(slot);
    post([path] {
        auto size = reinterpret_cast<size_t (*)()>(coreSym("retro_serialize_size"));
        auto ser = reinterpret_cast<bool (*)(void*, size_t)>(coreSym("retro_serialize"));
        const size_t n = size ? size() : 0;
        if (!n || !ser) { say("This emulator cannot save states"); return; }
        std::vector<uint8_t> d(n);
        if (!ser(d.data(), n) || !writeFile(path, d.data(), n)) { say("Could not save the state"); return; }
        say("State saved");
    });
}

void loadState(int slot) {
    const std::string path = statePathFor(slot);
    post([path] {
        auto unser = reinterpret_cast<bool (*)(const void*, size_t)>(coreSym("retro_unserialize"));
        std::vector<uint8_t> d;
        if (!readFile(path, d) || d.empty()) { say("No saved state yet"); return; }
        if (!unser || !unser(d.data(), d.size())) { say("Could not load the state"); return; }
        say("State loaded");
    });
}

bool hasState(int slot) { std::error_code ec; return fs::exists(statePathFor(slot), ec); }

int discCount() { return disksNow.load(); }
int discIndex() { return diskNow.load(); }

// the next disc: the tray opens, the disc is changed, and a second later it closes (the game sees the lid open and
// shut, as on the console)
void nextDisc() {
    post([] {
        if (!diskCount || !diskSetIndex || !diskEject || diskCount() < 2) return;
        const unsigned n = diskCount(), i = diskGetIndex ? diskGetIndex() : 0;
        diskEject(true);
        diskSetIndex((i + 1) % n);
        diskInsertIn = 60;
        diskNow = int((i + 1) % n);
        say("Disc changed");
    });
}

void reset() {
    post([] { if (auto f = reinterpret_cast<void (*)()>(coreSym("retro_reset"))) f(); });
}

void setPaused(bool p) { if (auto r = get()) r->paused = p; }

std::string takeMessage() { std::lock_guard l(msgM); std::string m; m.swap(message); return m; }

Info info() {
    auto r = get();
    if (!r) return {};
    std::lock_guard i(r->infoM);
    return r->info;
}

bool coreLoads(const std::string& corePath, std::string* name, std::string* extensions) {
    void* so = dlopen(corePath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!so) {
        std::fprintf(stderr, "Games: %s does not load: %s\n", corePath.c_str(), dlerror());
        return false;
    }
    auto fn = reinterpret_cast<void (*)(retro_system_info*)>(dlsym(so, "retro_get_system_info"));
    if (fn) {
        retro_system_info si{};
        fn(&si);
        if (extensions && si.valid_extensions) *extensions = si.valid_extensions;
        if (name && si.library_name) *name = si.library_name;
    }
    dlclose(so);
    return fn != nullptr;
}

// ------------------------------------------------------------------ Stream

Stream::~Stream() { close(); }

bool Stream::open(std::string& error) {
    close();
    Game g = game();
    if (g.core.empty() || g.rom.empty()) { error = "No game chosen"; return false; }
    {
        std::lock_guard l(runnerM);
        if (active) { error = "A game is already running"; return false; }
        active = std::make_shared<Runner>();
        runner = active.get();
    }
    takeMessage();
    Runner* r = runner;
    r->thread = std::thread([r, g] {
        pthread_setname_np(pthread_self(), "vx-retro");
        coreThread(r, g);
    });
    std::unique_lock l(r->loadM);
    r->loadCv.wait(l, [r] { return r->loadState != 0; });
    if (r->loadState < 0) {
        error = r->loadError;
        l.unlock();
        r->thread.join();
        std::lock_guard rl(runnerM);
        active.reset();
        runner = nullptr;
        return false;
    }
    l.unlock();
    open_ = true;
    seen_ = 0;
    // the first picture gives the size the source reports
    std::unique_lock fl(r->frameM);
    r->frameCv.wait_for(fl, std::chrono::seconds(3), [r] { return r->frameSeq > 0 || !r->running.load(); });
    return true;
}

void Stream::close() {
    if (!open_) return;
    open_ = false;
    auto r = get();
    if (!r) return;
    r->quit = true;
    r->frameCv.notify_all();
    if (r->thread.joinable()) r->thread.join();
    {
        std::lock_guard l(runnerM);
        active.reset();
        runner = nullptr;
    }
    std::lock_guard rl(rumbleM);
    if (rumbleCb && (rumbleStrong || rumbleWeak)) rumbleCb(0, 0);
    rumbleStrong = rumbleWeak = 0;
}

Stream::Result Stream::readLatest(RawFrame& out, int timeoutMs, std::string& error) {
    auto r = get();
    if (!open_ || !r) { error = "The game is not running"; return Result::Error; }
    std::unique_lock l(r->frameM);
    if (!r->frameCv.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return r->frameSeq != seen_ || r->quit.load() || !r->running.load(); }))
        return Result::Timeout;
    if (r->frameSeq == seen_) {
        if (!r->running.load() && r->loadState == 1) { error = "The game was closed"; return Result::Error; }
        return Result::Timeout;
    }
    seen_ = r->frameSeq;
    out.fmt = RawFrame::RGBA;
    out.data = r->frame;
    out.w = r->fw; out.h = r->fh; out.stride = r->fw * 4;
    out.repeat = false;
    out.reset = false;
    out.captured = r->frameAt;
    return Result::Frame;
}

double Stream::fps() const { return info().fps; }
int Stream::width() const {
    // the first picture decides; before it, the core's base size scaled the same way
    auto r = get();
    if (!r) return 0;
    std::lock_guard f(r->frameM);
    return r->fw > 0 ? r->fw : 1280;
}
int Stream::height() const {
    auto r = get();
    if (!r) return 0;
    std::lock_guard f(r->frameM);
    return r->fh > 0 ? r->fh : 960;
}
std::string Stream::name() const { const auto g = game(); return g.title.empty() ? baseName(g.rom) : g.title; }

// ------------------------------------------------------------------ archives (zip, 7z, rar: libarchive)

namespace {
struct Reader {
    archive* a = archive_read_new();
    Reader() { archive_read_support_filter_all(a); archive_read_support_format_zip(a); archive_read_support_format_7zip(a);
               archive_read_support_format_rar(a); archive_read_support_format_rar5(a); }
    ~Reader() { archive_read_free(a); }
    bool open(const std::string& path) { return archive_read_open_filename(a, path.c_str(), 1 << 16) == ARCHIVE_OK; }
};
} // namespace

bool isArchive(const std::string& path) {
    const std::string e = extOf(path);
    return e == "zip" || e == "7z" || e == "rar";
}

std::vector<ZipEntry> zipList(const std::string& zipPath) {
    std::vector<ZipEntry> out;
    Reader r;
    if (!r.open(zipPath)) return out;
    archive_entry* e;
    while (archive_read_next_header(r.a, &e) == ARCHIVE_OK) {
        if (archive_entry_filetype(e) == AE_IFREG && archive_entry_pathname(e))
            out.push_back({archive_entry_pathname(e), uint64_t(std::max<la_int64_t>(0, archive_entry_size(e)))});
        archive_read_data_skip(r.a);
    }
    return out;
}

bool zipRead(const std::string& zipPath, const std::string& name, std::vector<uint8_t>& out, std::string& error) {
    Reader r;
    if (!r.open(zipPath)) { error = "Not an archive VeyAmux can unpack: " + zipPath; return false; }
    archive_entry* e;
    while (archive_read_next_header(r.a, &e) == ARCHIVE_OK) {
        if (!archive_entry_pathname(e) || name != archive_entry_pathname(e)) { archive_read_data_skip(r.a); continue; }
        if (archive_entry_size(e) > (la_int64_t(1) << 30)) { error = "The file in the archive is too large"; return false; }
        out.clear();
        out.reserve(size_t(std::max<la_int64_t>(0, archive_entry_size(e))));
        uint8_t buf[1 << 16];
        for (;;) {
            const la_ssize_t n = archive_read_data(r.a, buf, sizeof buf);
            if (n == 0) return true;
            if (n < 0) { error = std::string("Broken archive: ") + archive_error_string(r.a); return false; }
            out.insert(out.end(), buf, buf + n);
        }
    }
    error = name + " is not in the archive";
    return false;
}

} // namespace veyamux::retro
