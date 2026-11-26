// VeyAmux for Linux: games in a process of their own (Libretro.h's frontend, run by "veyamux --retro-host").
//
// An emulator core is someone else's code, loaded into the process that runs it. When it crashes, that process goes
// with it. So VeyAmux runs it in a child process: the same program started with --retro-host, which runs Libretro.cpp
// as before. The picture, the sound and the controllers go through shared memory, the commands (save, load, reset,
// next disc, close) and the messages back through a socket, and the child's log lines into VeyAmux's log. When the
// emulator crashes, the game stops with "The emulator crashed" and VeyAmux carries on.
//
// Libretro.h's functions forward here by themselves once useHostProcess() was called (VeyAmux's main does; the
// retro test does it for its second half). VEYAMUX_RETRO_INPROC=1 runs games in VeyAmux's own process as before.
#pragma once
#include "Libretro.h"

#include <cstdint>
#include <functional>
#include <string>

namespace veyamux::retro {

// run games in a child process: `exe --retro-host` (exe: this program, /proc/self/exe)
void useHostProcess(const std::string& exe);
// the child's own entry, from main() when argv[1] is "--retro-host" (fds 3: the socket, 4: the shared memory)
int hostMain();
// where the child's log lines go (VeyAmux: qInfo, so they are in the log file)
void setHostLog(std::function<void(const std::string&)> sink);

namespace remote {
bool on();   // games go to a child process

void setGame(const Game& g);
void setPad(int port, uint32_t buttons, const int16_t axes[4]);
void setKeys(uint32_t buttons);
void setRunAhead(int frames);
void setRewind(bool on);
void setRewinding(bool held);
void setPaused(bool p);
void setRumbleCallback(std::function<void(int, int)> cb);
void audio(int16_t* out, int frames);
bool running();
void command(const std::string& cmd);   // "save 0", "load 0", "reset", "disc"
int discCount();
int discIndex();
std::string takeMessage();
struct InfoOut { std::string core; double fps = 0, sampleRate = 0; int w = 0, h = 0; float aspect = 0; };
InfoOut info();

bool open(std::string& error);
void close();
enum class Read { Frame, Timeout, Error };
Read readLatest(RawFrame& out, uint64_t& seen, int timeoutMs, std::string& error);
int width();
int height();
} // namespace remote

} // namespace veyamux::retro
