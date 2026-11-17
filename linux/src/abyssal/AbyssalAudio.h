// VeyAmux for Linux: Abyssal Garden demo scene, audio.js - the procedural underwater ambience (no sample files):
// rumble, surface wash, water rush that follows the swimming speed, a drone of stacked fifths, bubbles, chimes and
// distant whale calls. The Web Audio graph is rebuilt as a small synthesiser; the 5 s convolution reverb of the original
// is replaced by a Freeverb-style reverb with the same ~4.6 s decay.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

namespace abyssal {

class Synth {
public:
    explicit Synth(int rate = 48000);
    ~Synth();
    // interleaved stereo, signed 16 bit
    void render(int16_t* out, int frames);
    // camera position (any thread): the water rush follows the speed, the surface wash the depth
    void setCamera(double x, double y, double z);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace abyssal
