// VeyAmux for Linux: RetroAchievements in games (rcheevos, third_party/rcheevos, MIT).
//
// Runs on the emulator's core thread (Libretro.cpp; in the emulator's own process, RetroRemote.h): the game is
// identified by its hash, its achievements are fetched, every frame the game's memory is checked against them, and an
// unlock is sent to retroachievements.org and shown as a notice. The server calls go out with libcurl on threads of
// their own. Signing in (user name + password -> a token) is done by VeyAmux (GameLibrary); only the token comes here.
// Softcore: save states, rewind and run-ahead stay usable (hardcore mode, which forbids them, is not offered).
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

struct retro_memory_map;

namespace veyamux::retro::cheevos {

// rcheevos' console number for a Games page system id ("snes" -> 3), 0 when RetroAchievements has none
uint32_t consoleFor(const std::string& systemId);

struct Start {
    std::string user, token;          // the signed-in account
    uint32_t console = 0;
    std::string path;                 // the game file (as the core got it)
    const uint8_t* data = nullptr;    // or its bytes (unpacked from an archive)
    size_t size = 0;
    const retro_memory_map* memoryMap = nullptr;   // from the core (SET_MEMORY_MAPS), or null
    std::function<void*(unsigned id)> memoryData;  // retro_get_memory_data / _size
    std::function<size_t(unsigned id)> memorySize;
    std::function<void(const std::string&)> say;   // a notice for the player
};
// a game was loaded: identify it and fetch its achievements (nothing happens without an account or console)
void start(const Start& s);
void frame();                // after each real frame (not the run-ahead or rewind ones)
void idle();                 // while paused
void reset();                // the game was reset
// with a save state: the achievements' progress (a file next to the state)
void saveProgress(const std::string& statePath);
void loadProgress(const std::string& statePath);
void stop();                 // the game ends
bool active();

} // namespace veyamux::retro::cheevos
