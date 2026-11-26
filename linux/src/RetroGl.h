// VeyAmux for Linux: OpenGL for emulator cores that draw with it (N64, Dreamcast, PSP: RETRO_ENVIRONMENT_SET_HW_RENDER).
//
// An EGL context without a window (Mesa's surfaceless platform, else a GPU device, else the default display) on the
// core thread, with a framebuffer the core draws into. After each frame its picture is read back (glReadPixels) and
// goes the same way as a software core's: so upscaling, Retro TV, frame generation and recording work on it too.
// libEGL is loaded when a core asks for OpenGL, so VeyAmux needs no OpenGL libraries to build or to start.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct retro_hw_render_callback;

namespace veyamux::retro::gl {

// can this context be made (the core's SET_HW_RENDER): OpenGL 2-4.6 (compatibility or core) and OpenGL ES 2/3
bool supported(const retro_hw_render_callback& cb);
// made on the core thread after the game is loaded (the core's context_reset is called by the caller)
bool create(const retro_hw_render_callback& cb, int maxW, int maxH, std::string& error);
uintptr_t framebuffer();
void* procAddress(const char* name);
// the picture the core drew (w x h), as XRGB8888 rows top first
bool read(int w, int h, bool bottomLeftOrigin, std::vector<uint8_t>& out);
void destroy();
bool active();

} // namespace veyamux::retro::gl
