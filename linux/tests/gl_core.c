/* A libretro core that draws with OpenGL (RETRO_ENVIRONMENT_SET_HW_RENDER), the way N64 / Dreamcast / PSP emulators
 * do: for the test of VeyAmux's OpenGL path (Libretro.cpp). Each frame it clears its framebuffer to a colour that
 * depends on the frame and on button B, and draws a red square in the lower left corner (scissored clear) - so the test
 * sees that the picture arrives, the right way up, and that the input reaches the core. Built by
 * tests/run_retro_test.sh, never shipped. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../third_party/libretro/libretro.h"

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_FRAMEBUFFER 0x8D40
#define GL_SCISSOR_TEST 0x0C11

static void (*glClearColor_)(GLfloat, GLfloat, GLfloat, GLfloat);
static void (*glClear_)(GLbitfield);
static void (*glViewport_)(GLint, GLint, GLsizei, GLsizei);
static void (*glBindFramebuffer_)(GLenum, GLuint);
static void (*glEnable_)(GLenum);
static void (*glDisable_)(GLenum);
static void (*glScissor_)(GLint, GLint, GLsizei, GLsizei);

static struct retro_hw_render_callback hw;
static retro_environment_t env;
static retro_video_refresh_t video;
static retro_input_state_t input;
static retro_input_poll_t poll;
static unsigned frame;
static bool ready;

#define W 320
#define H 240

static void context_reset(void) {
    glClearColor_ = (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))hw.get_proc_address("glClearColor");
    glClear_ = (void (*)(GLbitfield))hw.get_proc_address("glClear");
    glViewport_ = (void (*)(GLint, GLint, GLsizei, GLsizei))hw.get_proc_address("glViewport");
    glBindFramebuffer_ = (void (*)(GLenum, GLuint))hw.get_proc_address("glBindFramebuffer");
    glEnable_ = (void (*)(GLenum))hw.get_proc_address("glEnable");
    glDisable_ = (void (*)(GLenum))hw.get_proc_address("glDisable");
    glScissor_ = (void (*)(GLint, GLint, GLsizei, GLsizei))hw.get_proc_address("glScissor");
    ready = glClearColor_ && glClear_ && glViewport_ && glBindFramebuffer_ && glEnable_ && glDisable_ && glScissor_;
    /* like PPSSPP's GLEW: with an X display, OpenGL is only loaded when a GLX context is current */
    const char* d = getenv("DISPLAY");
    if (d && *d) {
        void* (*current)(void) = (void* (*)(void))dlsym(RTLD_DEFAULT, "glXGetCurrentDisplay");
        if (!current || !current()) ready = false;
    }
}
static void context_destroy(void) { ready = false; }

RETRO_API void retro_set_environment(retro_environment_t cb) { env = cb; }
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { (void)cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { poll = cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { input = cb; }
RETRO_API void retro_init(void) {}
RETRO_API void retro_deinit(void) {}
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }
RETRO_API void retro_get_system_info(struct retro_system_info* i) {
    memset(i, 0, sizeof *i); i->library_name = "GL test"; i->library_version = "1"; i->valid_extensions = "gltest";
}
RETRO_API void retro_get_system_av_info(struct retro_system_av_info* i) {
    memset(i, 0, sizeof *i);
    i->geometry.base_width = W; i->geometry.base_height = H; i->geometry.max_width = W; i->geometry.max_height = H;
    i->geometry.aspect_ratio = 4.0f / 3.0f; i->timing.fps = 60; i->timing.sample_rate = 48000;
}
RETRO_API void retro_set_controller_port_device(unsigned p, unsigned d) { (void)p; (void)d; }
RETRO_API void retro_reset(void) { frame = 0; }
RETRO_API void retro_run(void) {
    poll();
    ++frame;
    if (!ready) { video(NULL, W, H, 0); return; }
    const bool b = input(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B) != 0;
    glBindFramebuffer_(GL_FRAMEBUFFER, (GLuint)hw.get_current_framebuffer());
    glViewport_(0, 0, W, H);
    glDisable_(GL_SCISSOR_TEST);
    /* the whole picture: green while B is held, else blue getting brighter */
    if (b) glClearColor_(0.0f, 1.0f, 0.0f, 1.0f);
    else glClearColor_(0.0f, 0.0f, (float)(frame % 60) / 60.0f, 1.0f);
    glClear_(GL_COLOR_BUFFER_BIT);
    /* a red square in OpenGL's lower left (the picture's bottom left once turned the right way up) */
    glEnable_(GL_SCISSOR_TEST);
    glScissor_(0, 0, 40, 40);
    glClearColor_(1.0f, 0.0f, 0.0f, 1.0f);
    glClear_(GL_COLOR_BUFFER_BIT);
    glDisable_(GL_SCISSOR_TEST);
    video(RETRO_HW_FRAME_BUFFER_VALID, W, H, 0);
}
RETRO_API size_t retro_serialize_size(void) { return sizeof frame; }
RETRO_API bool retro_serialize(void* d, size_t n) { if (n < sizeof frame) return false; memcpy(d, &frame, sizeof frame); return true; }
RETRO_API bool retro_unserialize(const void* d, size_t n) { if (n < sizeof frame) return false; memcpy(&frame, d, sizeof frame); return true; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned i, bool e, const char* c) { (void)i; (void)e; (void)c; }
RETRO_API bool retro_load_game(const struct retro_game_info* g) {
    (void)g;
    enum retro_pixel_format f = RETRO_PIXEL_FORMAT_XRGB8888;
    env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &f);
    memset(&hw, 0, sizeof hw);
    hw.context_type = RETRO_HW_CONTEXT_OPENGL_CORE;
    hw.version_major = 3;
    hw.version_minor = 3;
    hw.context_reset = context_reset;
    hw.context_destroy = context_destroy;
    hw.depth = true;
    hw.bottom_left_origin = true;
    return env(RETRO_ENVIRONMENT_SET_HW_RENDER, &hw);
}
RETRO_API bool retro_load_game_special(unsigned t, const struct retro_game_info* g, size_t n) { (void)t; (void)g; (void)n; return false; }
RETRO_API void retro_unload_game(void) {}
RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
RETRO_API void* retro_get_memory_data(unsigned id) { (void)id; return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
