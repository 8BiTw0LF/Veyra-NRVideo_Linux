// VeyAmux for Linux: Abyssal Garden demo scene, main.js - the renderer, camera and swimming (see AbyssalMath.h).
//
// The original WGSL shaders run unchanged through wgpu-native (WebGPU's C implementation on Vulkan). The picture is drawn
// at a chosen size (1920x1080 at most, also 1280x720) into an RGBA8 texture and read back, so VeyAmux can treat it like a capture card.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace abyssal {

// what the player is doing right now (filled by the demo source from keyboard, mouse and controller)
struct Controls {
    double forward = 0, right = 0, up = 0;   // -1..1 each (keys W/S, D/A, E/Q; left stick; triggers)
    bool fast = false;                       // Shift, left stick click
    double lookX = 0, lookY = 0;             // mouse movement since the last frame (pixels)
    double stickX = 0, stickY = 0;           // right stick, -1..1 (turn speed)
    int pose = -1;                           // jump to viewpoint 0..5
};

class Scene {
public:
    Scene();
    ~Scene();
    // Builds everything (a few seconds). progress(text, 0..1) is called between the stages.
    bool init(int width, int height, const std::function<void(const char*, double)>& progress, std::string& error);
    // Moves the camera by dt seconds and draws one frame into rgba (width*height*4, tightly packed).
    bool frame(double dt, const Controls& in, std::vector<uint8_t>& rgba);
    // Another picture size (the render targets are made anew, the world stays). false: the GPU refused.
    bool resize(int width, int height);
    int width() const;
    int height() const;
    // camera position and speed, for the sound
    void camera(double& x, double& y, double& z) const;
    std::string adapterName() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace abyssal
