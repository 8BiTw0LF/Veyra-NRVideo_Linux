// VeyAmux for Linux: the sound devices of PulseAudio / PipeWire (pipewire-pulse), read with libpulse itself.
// (Earlier this ran the pactl tool, which is not installed everywhere; then only "system default" was offered.)
#pragma once
#include <string>
#include <vector>

namespace veyamux::pulse {

struct Device {
    std::string name;          // PulseAudio name (what pa_simple / the settings use)
    std::string description;   // human-readable
    bool monitor = false;      // a source that records an output ("Monitor of ...")
};

// Output devices (sinks) and input devices (sources), in the server's order. Empty if no sound server answers within
// timeoutMs.
std::vector<Device> sinks(int timeoutMs = 2000);
std::vector<Device> sources(int timeoutMs = 2000);

} // namespace veyamux::pulse
