// VeyAmux for Linux: the demo scene "Abyssal Garden" as a source (capture path "demo:30" / "demo:60").
//
// A 3D underwater world rendered on the GPU at 1920x1080 and handed to VeyAmux like a capture card's picture, so every
// feature (upscaling, noise reduction, Retro TV, frame generation, recording, streaming) can be tried without a console.
// The player swims with the keyboard, mouse or a controller while the menu stays usable (see PlayerBridge::demoKey).
// The world is built once per run in a background thread (a few seconds) and kept for the next time.
#pragma once
#include <cstdint>
#include <string>

namespace veyamux { struct RawFrame; }

namespace veyamux::demo {

bool available();                  // built with wgpu-native
constexpr int kWidth = 1920, kHeight = 1080;

// ---- input (any thread)
enum class Key { Forward, Back, Left, Right, Up, Down, Fast };
void setKey(Key k, bool down);
void addLook(double dx, double dy);   // mouse movement in pixels
// controller: left stick (move), right stick (look), triggers (down / up), stick click (fast)
void setPad(double lx, double ly, double rx, double ry, double down, double up, bool fast);
void jumpTo(int pose);                // viewpoint 0..5
void releaseAll();

// ---- state for the UI
double progress();                    // 0..1 while the world is built, 1 when it runs
std::string stage();                  // what is being built, or the error
bool failed();

// ---- sound (the audio thread of the capture): 48 kHz interleaved stereo
void audio(int16_t* out, int frames);

// ---- the picture
class Stream {
public:
    ~Stream();
    bool open(int fps, std::string& error);
    void close();
    enum class Result { Frame, Timeout, Error };
    // newest picture (RGBA); while the world is built, a dark picture with a progress bar
    Result readLatest(RawFrame& out, int timeoutMs, std::string& error);
    int fps() const { return fps_; }

private:
    int fps_ = 0;
    uint64_t seen_ = 0;
    bool open_ = false;
};

// stops the render thread (before the process exits)
void shutdown();

} // namespace veyamux::demo
