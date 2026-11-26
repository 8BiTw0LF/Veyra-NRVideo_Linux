// VeyAmux for Linux: games in a process of their own. See RetroRemote.h.
#include "RetroRemote.h"
#include "CaptureSource.h"   // RawFrame

#include <execinfo.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

extern char** environ;

namespace veyamux::retro {

namespace {

using clk = std::chrono::steady_clock;

// ------------------------------------------------------------------ what both processes share
constexpr int kMaxW = 4096, kMaxH = 2304;            // the largest picture (a core's output scaled up by a whole number)
constexpr size_t kFrameBytes = size_t(kMaxW) * kMaxH * 4;
constexpr int kSlots = 3;                            // the child writes one while VeyAmux reads another
constexpr uint64_t kRing = 48000;                    // sound: one second at 48 kHz (stereo frames)

struct Shared {
    // VeyAmux -> the child
    std::atomic<uint32_t> pad[4];
    std::atomic<int16_t> axes[4][4];
    std::atomic<uint32_t> keys;
    std::atomic<int> runAhead, rewind, rewinding, paused;
    std::atomic<uint64_t> audioRead;                 // sound frames VeyAmux took
    // the child -> VeyAmux
    std::atomic<int> running, discCount, discIndex;
    std::atomic<int> rumbleLow, rumbleHigh;
    std::atomic<uint32_t> rumbleSeq;
    std::atomic<uint32_t> infoSeq;                   // odd while the info is written
    char infoCore[128];
    double infoFps, infoRate;
    int infoW, infoH;
    float infoAspect;
    std::atomic<uint64_t> audioWritten;              // sound frames the child put in the ring
    int16_t ring[kRing * 2];
    std::atomic<uint64_t> frameSeq;                  // the newest picture is in slot frameSeq % kSlots
    int fw[kSlots], fh[kSlots];
    int64_t frameAtNs[kSlots];
    alignas(64) uint8_t frames[kSlots][kFrameBytes];
};
static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<int16_t>::is_always_lock_free,
              "the shared counters must work across processes");

Shared* mapShared(int fd) {
    void* p = mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    return p == MAP_FAILED ? nullptr : static_cast<Shared*>(p);
}

// one packet per message (SOCK_SEQPACKET keeps them apart)
bool sendMsg(int fd, const std::string& m) { return ::send(fd, m.data(), m.size(), MSG_NOSIGNAL) == ssize_t(m.size()); }
// -1: the other side is gone, 0: nothing within timeoutMs, 1: a message
int recvMsg(int fd, std::string& m, int timeoutMs) {
    pollfd p{fd, POLLIN, 0};
    const int n = ::poll(&p, 1, timeoutMs);
    if (n == 0) return 0;
    if (n < 0) return errno == EINTR ? 0 : -1;
    char buf[8192];
    const ssize_t r = ::recv(fd, buf, sizeof buf, 0);
    if (r <= 0) return -1;
    m.assign(buf, size_t(r));
    return 1;
}

constexpr char kSep = '\x1f';
std::string packGame(const Game& g) {
    std::string s;
    for (const std::string* f : {&g.core, &g.rom, &g.title, &g.systemDir, &g.saveDir, &g.stateDir}) s += *f + kSep;
    s += std::to_string(g.stickAsDpad) + kSep + std::to_string(g.resume) + kSep + std::to_string(g.runAhead) + kSep + std::to_string(g.rewind);
    s += kSep + g.raUser + kSep + g.raToken + kSep + std::to_string(g.raConsole);
    return s;
}
Game unpackGame(const std::string& s) {
    std::vector<std::string> f;
    size_t a = 0;
    for (size_t b; (b = s.find(kSep, a)) != std::string::npos; a = b + 1) f.push_back(s.substr(a, b - a));
    f.push_back(s.substr(a));
    Game g;
    if (f.size() < 10) return g;
    g.core = f[0]; g.rom = f[1]; g.title = f[2]; g.systemDir = f[3]; g.saveDir = f[4]; g.stateDir = f[5];
    g.stickAsDpad = f[6] == "1"; g.resume = f[7] == "1"; g.runAhead = std::atoi(f[8].c_str()); g.rewind = f[9] == "1";
    if (f.size() >= 13) { g.raUser = f[10]; g.raToken = f[11]; g.raConsole = std::atoi(f[12].c_str()); }
    return g;
}

// ------------------------------------------------------------------ VeyAmux's side
std::string hostExe;
std::mutex logM;
std::function<void(const std::string&)> logSink;
void hostLog(const std::string& line) {
    std::lock_guard l(logM);
    if (logSink) logSink(line);
    else std::fprintf(stderr, "%s\n", line.c_str());
}

struct Client {
    int shmFd = -1;
    Shared* sh = nullptr;
    // the child of the game running
    pid_t pid = -1;
    int sock = -1, errPipe = -1;
    std::thread poller;
    std::atomic<bool> stop{false}, dead{false};
    std::atomic<int> exitStatus{0};
    bool reaped = false;
    uint64_t audioRead = 0;
    Game game;
    std::mutex msgM;
    std::string message;
    std::string openAnswer;   // "ok" or "err <why>" (its own: the UI reads the messages meanwhile)
    std::mutex rumbleM;
    std::function<void(int, int)> rumbleCb;
    uint32_t rumbleSeen = 0;
};
Client cl;
std::mutex clientM;   // open / close

bool ensureShared() {
    if (cl.sh) return true;
    cl.shmFd = memfd_create("veyamux-retro", MFD_CLOEXEC);
    if (cl.shmFd < 0 || ftruncate(cl.shmFd, off_t(sizeof(Shared))) != 0) return false;
    cl.sh = mapShared(cl.shmFd);
    return cl.sh != nullptr;   // (a new memfd is zero: every counter starts at 0)
}

void say(const std::string& m) { std::lock_guard l(cl.msgM); cl.message = m; }

// the child's messages, its log lines, its rumble, and its end
void pollLoop() {
    std::string pending;   // a log line not finished yet
    while (!cl.stop.load()) {
        pollfd p[2] = {{cl.sock, POLLIN, 0}, {cl.errPipe, POLLIN, 0}};
        const int n = ::poll(p, 2, 10);
        if (n > 0 && (p[0].revents & (POLLIN | POLLHUP))) {
            char buf[8192];
            const ssize_t r = ::recv(cl.sock, buf, sizeof buf, MSG_DONTWAIT);
            if (r > 0) {
                const std::string m(buf, size_t(r));
                if (m.rfind("open ", 0) == 0) { std::lock_guard l(cl.msgM); cl.openAnswer = m.substr(5); }
                else if (m.rfind("msg ", 0) == 0) say(m.substr(4));
            } else if (r == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                cl.dead = true;   // the socket closed: the child is gone (or going)
            }
        }
        if (n > 0 && (p[1].revents & (POLLIN | POLLHUP))) {
            char buf[4096];
            const ssize_t r = ::read(cl.errPipe, buf, sizeof buf);
            if (r > 0) {
                pending.append(buf, size_t(r));
                for (size_t e; (e = pending.find('\n')) != std::string::npos; pending.erase(0, e + 1))
                    if (e > 0) hostLog("Emulator: " + pending.substr(0, e));
            }
        }
        if (cl.sh && cl.sh->rumbleSeq.load() != cl.rumbleSeen) {
            cl.rumbleSeen = cl.sh->rumbleSeq.load();
            std::lock_guard l(cl.rumbleM);
            if (cl.rumbleCb) cl.rumbleCb(cl.sh->rumbleLow.load(), cl.sh->rumbleHigh.load());
        }
        if (cl.dead.load() && !cl.reaped) {
            int st = 0;
            if (::waitpid(cl.pid, &st, WNOHANG) == cl.pid) {
                cl.reaped = true;
                cl.exitStatus = st;
                if (WIFSIGNALED(st)) hostLog("Emulator: crashed (signal " + std::to_string(WTERMSIG(st)) + ")");
            }
        }
    }
}

bool alive() { return cl.pid > 0 && !cl.dead.load(); }

std::string endReason() {
    const int st = cl.exitStatus.load();
    if (cl.reaped && (WIFSIGNALED(st) || (WIFEXITED(st) && WEXITSTATUS(st) != 0))) return "The emulator crashed";
    return "The game was closed";
}

void reapNow(int waitMs) {
    if (cl.pid <= 0 || cl.reaped) return;
    const auto until = clk::now() + std::chrono::milliseconds(waitMs);
    int st = 0;
    while (clk::now() < until) {
        if (::waitpid(cl.pid, &st, WNOHANG) == cl.pid) { cl.reaped = true; cl.exitStatus = st; return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ::kill(cl.pid, SIGKILL);   // does not quit: stopped hard
    if (::waitpid(cl.pid, &st, 0) == cl.pid) { cl.reaped = true; cl.exitStatus = st; }
}

void closeFds() {
    if (cl.sock >= 0) ::close(cl.sock);
    if (cl.errPipe >= 0) ::close(cl.errPipe);
    cl.sock = cl.errPipe = -1;
}

} // namespace

void useHostProcess(const std::string& exe) { hostExe = exe; }
void setHostLog(std::function<void(const std::string&)> sink) { std::lock_guard l(logM); logSink = std::move(sink); }

namespace remote {

bool on() { return !hostExe.empty(); }

void setGame(const Game& g) { cl.game = g; }
void setPad(int port, uint32_t buttons, const int16_t axes[4]) {
    if (!ensureShared() || port < 0 || port > 3) return;
    cl.sh->pad[port] = buttons;
    for (int k = 0; k < 4; ++k) cl.sh->axes[port][k] = axes[k];
}
void setKeys(uint32_t buttons) { if (ensureShared()) cl.sh->keys = buttons; }
void setRunAhead(int frames) { if (ensureShared()) cl.sh->runAhead = frames; }
void setRewind(bool on) { if (ensureShared()) cl.sh->rewind = on; }
void setRewinding(bool held) { if (ensureShared()) cl.sh->rewinding = held; }
void setPaused(bool p) { if (ensureShared()) cl.sh->paused = p; }
void setRumbleCallback(std::function<void(int, int)> cb) { std::lock_guard l(cl.rumbleM); cl.rumbleCb = std::move(cb); }

// sound: the child keeps a little ahead in the ring; this waits for a block like Libretro's own audio() does
void audio(int16_t* out, int frames) {
    Shared* sh = cl.sh;
    if (!sh || !alive()) {
        std::memset(out, 0, size_t(frames) * 4);
        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, frames / 48)));
        return;
    }
    const auto until = clk::now() + std::chrono::milliseconds(frames / 48 + 30);
    while (sh->audioWritten.load() - cl.audioRead < uint64_t(frames) && alive() && clk::now() < until)
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    const uint64_t have = std::min<uint64_t>(sh->audioWritten.load() - cl.audioRead, uint64_t(frames));
    for (uint64_t i = 0; i < have; ++i) {
        const uint64_t at = (cl.audioRead + i) % kRing;
        out[i * 2] = sh->ring[at * 2];
        out[i * 2 + 1] = sh->ring[at * 2 + 1];
    }
    for (int i = int(have); i < frames; ++i) out[i * 2] = out[i * 2 + 1] = 0;
    cl.audioRead += have;
    sh->audioRead = cl.audioRead;
}

bool running() { return alive() && cl.sh && cl.sh->running.load(); }
void command(const std::string& cmd) { if (alive()) sendMsg(cl.sock, cmd); }
int discCount() { return alive() && cl.sh ? cl.sh->discCount.load() : 0; }
int discIndex() { return alive() && cl.sh ? cl.sh->discIndex.load() : 0; }
std::string takeMessage() { std::lock_guard l(cl.msgM); std::string m; m.swap(cl.message); return m; }

InfoOut info() {
    InfoOut o;
    Shared* sh = cl.sh;
    if (!sh || cl.pid <= 0) return o;
    for (int tries = 0; tries < 50; ++tries) {
        const uint32_t a = sh->infoSeq.load();
        if (a & 1) { std::this_thread::yield(); continue; }
        o.core.assign(sh->infoCore, strnlen(sh->infoCore, sizeof sh->infoCore));
        o.fps = sh->infoFps; o.sampleRate = sh->infoRate; o.w = sh->infoW; o.h = sh->infoH; o.aspect = sh->infoAspect;
        if (sh->infoSeq.load() == a) break;
    }
    return o;
}

bool open(std::string& error) {
    std::lock_guard l(clientM);
    if (!ensureShared()) { error = "Could not start the emulator (no shared memory)"; return false; }
    if (cl.pid > 0) close();
    Shared* sh = cl.sh;
    // a fresh start: no picture, no sound, nothing running
    sh->frameSeq = 0; sh->audioWritten = 0; sh->audioRead = 0; sh->running = 0; sh->discCount = 0; sh->discIndex = 0;
    sh->rewinding = 0; sh->paused = 0; sh->runAhead = cl.game.runAhead; sh->rewind = cl.game.rewind;
    sh->infoSeq = 0; sh->infoCore[0] = 0; sh->infoFps = 0; sh->infoW = sh->infoH = 0;
    cl.audioRead = 0;
    { std::lock_guard m(cl.msgM); cl.openAnswer.clear(); }
    cl.dead = false; cl.stop = false; cl.reaped = false; cl.exitStatus = 0; cl.rumbleSeen = sh->rumbleSeq.load();
    takeMessage();

    int sv[2], ep[2];
    if (::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sv) != 0) { error = "Could not start the emulator"; return false; }
    if (::pipe2(ep, O_CLOEXEC) != 0) { ::close(sv[0]); ::close(sv[1]); error = "Could not start the emulator"; return false; }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, sv[1], 3);
    posix_spawn_file_actions_adddup2(&fa, cl.shmFd, 4);
    posix_spawn_file_actions_adddup2(&fa, ep[1], 2);
    posix_spawn_file_actions_adddup2(&fa, ep[1], 1);
    char arg0[] = "veyamux-emulator", arg1[] = "--retro-host";
    char* argv[] = {arg0, arg1, nullptr};
    pid_t pid = -1;
    const int rc = posix_spawn(&pid, hostExe.c_str(), &fa, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    ::close(sv[1]);
    ::close(ep[1]);
    if (rc != 0) {
        ::close(sv[0]); ::close(ep[0]);
        error = "Could not start the emulator: " + std::string(std::strerror(rc));
        return false;
    }
    cl.pid = pid;
    cl.sock = sv[0];
    cl.errPipe = ep[0];
    cl.poller = std::thread([] { pthread_setname_np(pthread_self(), "vx-retro-host"); pollLoop(); });

    // the game, then "open": the answer is "ok", or "err <why>"
    sendMsg(cl.sock, "game " + packGame(cl.game));
    sendMsg(cl.sock, "open");
    const auto until = clk::now() + std::chrono::seconds(120);   // (a large disc image in a 7z file takes a while)
    std::string answer;
    while (clk::now() < until && !cl.dead.load()) {
        {   // (the poller reads the socket)
            std::lock_guard m(cl.msgM);
            if (!cl.openAnswer.empty()) { answer = cl.openAnswer; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (answer != "ok") {
        if (answer.rfind("err ", 0) == 0) error = answer.substr(4);
        else if (cl.dead.load()) { reapNow(2000); error = "The emulator crashed while starting the game"; }
        else error = "The emulator did not start the game";
        close();
        return false;
    }
    // the first picture gives the size the source reports
    const auto first = clk::now() + std::chrono::seconds(3);
    while (sh->frameSeq.load() == 0 && alive() && clk::now() < first) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return true;
}

void close() {
    if (cl.pid <= 0) return;
    if (!cl.dead.load()) sendMsg(cl.sock, "close");
    reapNow(8000);   // it writes the battery save and the automatic state, then quits
    cl.stop = true;
    if (cl.poller.joinable()) cl.poller.join();
    // what it still said on the way out
    {
        char buf[4096];
        ssize_t r;
        std::string rest;
        while ((r = ::read(cl.errPipe, buf, sizeof buf)) > 0) rest.append(buf, size_t(r));
        for (size_t a = 0, e; (e = rest.find('\n', a)) != std::string::npos; a = e + 1)
            if (e > a) hostLog("Emulator: " + rest.substr(a, e - a));
    }
    closeFds();
    cl.pid = -1;
    if (cl.sh) cl.sh->running = 0;
    std::lock_guard rl(cl.rumbleM);
    if (cl.rumbleCb) cl.rumbleCb(0, 0);
}

Read readLatest(RawFrame& out, uint64_t& seen, int timeoutMs, std::string& error) {
    Shared* sh = cl.sh;
    if (!sh || cl.pid <= 0) { error = "The game is not running"; return Read::Error; }
    const auto until = clk::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        const uint64_t seq = sh->frameSeq.load();
        if (seq != seen && seq > 0) {
            const int s = int(seq % kSlots);
            const int w = sh->fw[s], h = sh->fh[s];
            out.fmt = RawFrame::RGBA;
            out.data.assign(sh->frames[s], sh->frames[s] + size_t(w) * h * 4);
            out.w = w; out.h = h; out.stride = w * 4;
            out.repeat = false;
            out.reset = false;
            out.captured = clk::time_point(std::chrono::nanoseconds(sh->frameAtNs[s]));
            seen = seq;
            return Read::Frame;
        }
        if (cl.dead.load()) {
            reapNow(1500);
            error = endReason();
            return Read::Error;
        }
        if (clk::now() >= until) return Read::Timeout;
        std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
}

int width() { const Shared* sh = cl.sh; const uint64_t q = sh ? sh->frameSeq.load() : 0; return q ? sh->fw[q % kSlots] : 1280; }
int height() { const Shared* sh = cl.sh; const uint64_t q = sh ? sh->frameSeq.load() : 0; return q ? sh->fh[q % kSlots] : 960; }

} // namespace remote

// ------------------------------------------------------------------ the child

namespace {
void onHostCrash(int sig) {
    // the backtrace into VeyAmux's log (stderr is the pipe to it), then the default end
    const char head[] = "the emulator crashed; backtrace:\n";
    (void)!::write(2, head, sizeof head - 1);
    void* frames[48];
    backtrace_symbols_fd(frames, backtrace(frames, 48), 2);
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}
} // namespace

int hostMain() {
    const int sock = 3;
    Shared* sh = mapShared(4);
    if (!sh) { std::fprintf(stderr, "no shared memory\n"); return 2; }
    std::setvbuf(stderr, nullptr, _IOLBF, 0);
    void* warm[2];
    backtrace(warm, 2);   // loads libgcc now, not inside the signal handler
    for (int s : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL}) ::signal(s, onHostCrash);
    ::signal(SIGPIPE, SIG_IGN);

    auto reply = [&](const std::string& m) { sendMsg(sock, "msg " + m); };
    // the rumble the game asks for
    setRumbleCallback([sh](int low, int high) { sh->rumbleLow = low; sh->rumbleHigh = high; ++sh->rumbleSeq; });

    Stream stream;
    std::atomic<bool> stop{false}, ended{false};   // ended: the game closed itself (the child quits too)
    std::thread video, sound, sync;
    std::string m;
    for (;;) {
        if (ended.load()) break;
        const int r = recvMsg(sock, m, 100);
        if (r < 0) break;   // VeyAmux is gone
        if (r == 0) continue;
        if (m.rfind("game ", 0) == 0) {
            setGame(unpackGame(m.substr(5)));
        } else if (m == "open") {
            std::string err;
            if (!stream.open(err)) { sendMsg(sock, "open err " + err); break; }
            {   // what runs, before the answer: the first picture may come before the sync thread's first round
                const Info i = info();
                sh->infoSeq.fetch_add(1);
                std::snprintf(sh->infoCore, sizeof sh->infoCore, "%s", i.core.c_str());
                sh->infoFps = i.fps; sh->infoRate = i.sampleRate; sh->infoW = i.w; sh->infoH = i.h; sh->infoAspect = i.aspect;
                sh->infoSeq.fetch_add(1);
                sh->running = 1;
            }
            sendMsg(sock, "open ok");
            // the pictures into the shared slots
            video = std::thread([&] {
                RawFrame f;
                std::string e;
                while (!stop.load()) {
                    const auto res = stream.readLatest(f, 50, e);
                    if (res == Stream::Result::Error) { sh->running = 0; ended = true; break; }
                    if (res != Stream::Result::Frame || f.w <= 0 || f.h <= 0 || f.w > kMaxW || f.h > kMaxH) continue;
                    const uint64_t next = sh->frameSeq.load() + 1;
                    const int s = int(next % kSlots);
                    std::memcpy(sh->frames[s], f.data.data(), size_t(f.w) * f.h * 4);
                    sh->fw[s] = f.w; sh->fh[s] = f.h;
                    sh->frameAtNs[s] = std::chrono::duration_cast<std::chrono::nanoseconds>(f.captured.time_since_epoch()).count();
                    sh->frameSeq = next;
                }
            });
            // the sound, kept 10 ms ahead of what VeyAmux took
            sound = std::thread([&] {
                std::vector<int16_t> buf(240 * 2);
                while (!stop.load()) {
                    if (sh->audioWritten.load() - sh->audioRead.load() >= 480) {
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                        continue;
                    }
                    audio(buf.data(), 240);
                    const uint64_t w = sh->audioWritten.load();
                    for (uint64_t i = 0; i < 240; ++i) {
                        const uint64_t at = (w + i) % kRing;
                        sh->ring[at * 2] = buf[i * 2];
                        sh->ring[at * 2 + 1] = buf[i * 2 + 1];
                    }
                    sh->audioWritten = w + 240;
                }
            });
            // the controllers and switches in, the state and messages out
            sync = std::thread([&] {
                int16_t ax[4][4];
                uint32_t pads[4]{}, keys = 0;
                int lastRunAhead = -1, lastRewind = -1, lastRewinding = -1, lastPaused = -1;
                while (!stop.load()) {
                    for (int p = 0; p < 4; ++p) {
                        bool changed = pads[p] != sh->pad[p].load();
                        pads[p] = sh->pad[p].load();
                        for (int k = 0; k < 4; ++k) { const int16_t v = sh->axes[p][k].load(); changed |= v != ax[p][k]; ax[p][k] = v; }
                        if (changed) setPad(p, pads[p], ax[p][0] / 32767.0, ax[p][1] / 32767.0, ax[p][2] / 32767.0, ax[p][3] / 32767.0);
                    }
                    if (keys != sh->keys.load()) { keys = sh->keys.load(); setKeys(keys); }
                    if (lastRunAhead != sh->runAhead.load()) setRunAhead(lastRunAhead = sh->runAhead.load());
                    if (lastRewind != sh->rewind.load()) setRewind((lastRewind = sh->rewind.load()) != 0);
                    if (lastRewinding != sh->rewinding.load()) setRewinding((lastRewinding = sh->rewinding.load()) != 0);
                    if (lastPaused != sh->paused.load()) setPaused((lastPaused = sh->paused.load()) != 0);
                    sh->running = running() ? 1 : 0;
                    sh->discCount = discCount();
                    sh->discIndex = discIndex();
                    const Info i = info();
                    sh->infoSeq.fetch_add(1);
                    std::snprintf(sh->infoCore, sizeof sh->infoCore, "%s", i.core.c_str());
                    sh->infoFps = i.fps; sh->infoRate = i.sampleRate; sh->infoW = i.w; sh->infoH = i.h; sh->infoAspect = i.aspect;
                    sh->infoSeq.fetch_add(1);
                    if (std::string t = takeMessage(); !t.empty()) reply(t);
                    std::this_thread::sleep_for(std::chrono::microseconds(500));
                }
            });
        } else if (m.rfind("save ", 0) == 0) {
            saveState(std::atoi(m.c_str() + 5));
        } else if (m.rfind("load ", 0) == 0) {
            loadState(std::atoi(m.c_str() + 5));
        } else if (m == "reset") {
            reset();
        } else if (m == "disc") {
            nextDisc();
        } else if (m == "close") {
            break;
        }
    }
    stop = true;
    if (video.joinable()) video.join();
    if (sound.joinable()) sound.join();
    if (sync.joinable()) sync.join();
    stream.close();   // the battery save and the automatic state are written here
    if (std::string t = takeMessage(); !t.empty()) reply(t);
    std::fflush(stderr);
    return 0;
}

} // namespace veyamux::retro
