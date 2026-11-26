/* A libretro core that shows a few pictures and then crashes: for the test that a crashing emulator only takes its own
 * process with it (RetroRemote.h). Built by tests/run_retro_test.sh, never shipped. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "../third_party/libretro/libretro.h"

static retro_video_refresh_t video;
static retro_environment_t env;
static uint32_t pixels[64 * 48];
static int frame;

RETRO_API void retro_set_environment(retro_environment_t cb) { env = cb; }
RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) { video = cb; }
RETRO_API void retro_set_audio_sample(retro_audio_sample_t cb) { (void)cb; }
RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) { (void)cb; }
RETRO_API void retro_set_input_poll(retro_input_poll_t cb) { (void)cb; }
RETRO_API void retro_set_input_state(retro_input_state_t cb) { (void)cb; }
RETRO_API void retro_init(void) {}
RETRO_API void retro_deinit(void) {}
RETRO_API unsigned retro_api_version(void) { return RETRO_API_VERSION; }
RETRO_API void retro_get_system_info(struct retro_system_info* i) {
    memset(i, 0, sizeof *i); i->library_name = "Crash test"; i->library_version = "1"; i->valid_extensions = "crash";
}
RETRO_API void retro_get_system_av_info(struct retro_system_av_info* i) {
    memset(i, 0, sizeof *i);
    i->geometry.base_width = 64; i->geometry.base_height = 48; i->geometry.max_width = 64; i->geometry.max_height = 48;
    i->geometry.aspect_ratio = 4.0f / 3.0f; i->timing.fps = 60; i->timing.sample_rate = 48000;
}
RETRO_API void retro_set_controller_port_device(unsigned p, unsigned d) { (void)p; (void)d; }
RETRO_API void retro_reset(void) {}
RETRO_API void retro_run(void) {
    if (++frame == 40) { volatile int* nothing = NULL; *nothing = 1; }   /* the crash */
    video(pixels, 64, 48, 64 * 4);
}
RETRO_API size_t retro_serialize_size(void) { return 0; }
RETRO_API bool retro_serialize(void* d, size_t n) { (void)d; (void)n; return false; }
RETRO_API bool retro_unserialize(const void* d, size_t n) { (void)d; (void)n; return false; }
RETRO_API void retro_cheat_reset(void) {}
RETRO_API void retro_cheat_set(unsigned i, bool e, const char* c) { (void)i; (void)e; (void)c; }
RETRO_API bool retro_load_game(const struct retro_game_info* g) {
    (void)g;
    enum retro_pixel_format f = RETRO_PIXEL_FORMAT_XRGB8888;
    env(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &f);
    return true;
}
RETRO_API bool retro_load_game_special(unsigned t, const struct retro_game_info* g, size_t n) { (void)t; (void)g; (void)n; return false; }
RETRO_API void retro_unload_game(void) {}
RETRO_API unsigned retro_get_region(void) { return RETRO_REGION_NTSC; }
RETRO_API void* retro_get_memory_data(unsigned id) { (void)id; return NULL; }
RETRO_API size_t retro_get_memory_size(unsigned id) { (void)id; return 0; }
