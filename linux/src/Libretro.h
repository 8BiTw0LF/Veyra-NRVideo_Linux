// VeyAmux for Linux: a minimal libretro frontend (capture path "retro:"), for the Games page.
//
// One emulator core (a libretro .so: Snes9x, Genesis Plus GX, Mesen, ...) runs one game on its own thread, paced at
// the core's frame rate. Its picture is handed to VeyAmux like a capture card's (RGBA, the pixels scaled up by a whole
// number with the core's aspect ratio, so every feature works on it: upscaling, Retro TV, frame generation,
// recording); its sound goes out through the capture audio path ("@retro"); the controllers come from Gamepad.h.
// Only cores that draw in software are used (no OpenGL / Vulkan cores).
//
// The game to run is set with setGame() before the source is opened, like Remote Play's stream config.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace veyamux { struct RawFrame; }

namespace veyamux::retro {

struct Game {
    std::string core;        // the core (.so)
    std::string rom;         // the game file (.zip / .7z / .rar is unpacked when the core cannot read it)
    std::string title;       // shown as the source's name
    std::string systemDir;   // BIOS files
    std::string saveDir;     // battery saves (.srm)
    std::string stateDir;    // save states
    bool stickAsDpad = true; // the left stick also works as the D-pad (systems without analog sticks)
    bool resume = false;     // continue where the game was quit (its automatic state)
    int runAhead = 0;        // frames run ahead (0-2): the game's own input delay taken away
    bool rewind = false;     // keep the last seconds, to step back through them
};
void setGame(const Game& g);
Game game();

// ---- input (any thread). Buttons: bits of RETRO_DEVICE_ID_JOYPAD_* (B = bit 0 ... R3 = bit 15).
// The controllers (all of them as player 1) and the keyboard are kept apart and combined.
void setPad(uint32_t buttons, double lx, double ly, double rx, double ry);
void setPad(int port, uint32_t buttons, double lx, double ly, double rx, double ry);   // players 1-4 (port 0-3)
void setKeys(uint32_t buttons);
// PlayStation layout bits (remoteplay::Button, from Gamepad.h's padState) to libretro buttons
uint32_t fromPlayStation(uint32_t ps, double l2, double r2);
void setRumbleCallback(std::function<void(int low, int high)> cb);   // 0..255 each

// ---- sound (the capture audio thread): 48 kHz interleaved stereo, resampled from the core's rate
void audio(int16_t* out, int frames);

// ---- while a game runs (any thread; carried out between two frames)
bool running();
void saveState(int slot);
void loadState(int slot);
bool hasState(int slot);
void reset();
// run ahead (0-2 frames) and rewind, also while the game runs; rewinding while held
void setRunAhead(int frames);
void setRewind(bool on);
void setRewinding(bool held);
// the automatic state written when the game is quit ("Continue")
std::string autoStatePath(const std::string& stateDir, const std::string& rom);
// games on several discs (.m3u): how many, which one is in (0 = the first), change to the next
int discCount();
int discIndex();
void nextDisc();
void setPaused(bool p);
// the last thing that went wrong, or a done message ("State saved") for the UI; cleared when read
std::string takeMessage();

// what is running ("Snes9x 1.62", 60.1 fps, 256x224)
struct Info { std::string core; double fps = 0; double sampleRate = 0; int w = 0, h = 0; float aspect = 0; };
Info info();

// does the core load here (built for this system, all its libraries present)? Its name and the file types it reads
// ("sfc|smc|zip") when it does
bool coreLoads(const std::string& corePath, std::string* name = nullptr, std::string* extensions = nullptr);

// ---- the picture
class Stream {
public:
    ~Stream();
    bool open(std::string& error);     // loads the core and the game set with setGame()
    void close();                      // saves the battery save and unloads
    enum class Result { Frame, Timeout, Error };
    Result readLatest(RawFrame& out, int timeoutMs, std::string& error);
    double fps() const;
    int width() const;
    int height() const;
    std::string name() const;
private:
    bool open_ = false;
    uint64_t seen_ = 0;
};

// ---- archives: zip, 7z, rar (also used for downloaded cores): the files in it, and one of them unpacked
bool isArchive(const std::string& path);
struct ZipEntry { std::string name; uint64_t size = 0; };
std::vector<ZipEntry> zipList(const std::string& zipPath);
bool zipRead(const std::string& zipPath, const std::string& name, std::vector<uint8_t>& out, std::string& error);

} // namespace veyamux::retro
