// VeyAmux for Linux: test of the libretro frontend (Libretro.h) with a core.
//   veyamux-retro-test <core.so> <game file> [crash_core.so [gl_core.so]]
// Runs the game for a second: pictures arrive at the core's rate in the expected size, sound comes out, a state can
// be saved and loaded, and a zipped copy of the game loads too. Exit code 0 when all of it worked.
#include "Libretro.h"
#include "CaptureSource.h"
#include "RetroRemote.h"

#include <archive.h>
#include <archive_entry.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

using namespace veyamux;
namespace fs = std::filesystem;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) ++failures;
}

// a stored (uncompressed) zip with one file, for the zip path
static void writeZip(const std::string& path, const std::string& name, const std::vector<uint8_t>& data) {
    auto le16 = [](std::vector<uint8_t>& v, uint16_t x) { v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8)); };
    auto le32 = [&](std::vector<uint8_t>& v, uint32_t x) { le16(v, uint16_t(x)); le16(v, uint16_t(x >> 16)); };
    uint32_t crc = 0xffffffffu;
    for (uint8_t b : data) { crc ^= b; for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1))); }
    crc ^= 0xffffffffu;
    std::vector<uint8_t> z;
    le32(z, 0x04034b50); le16(z, 20); le16(z, 0); le16(z, 0); le16(z, 0); le16(z, 0); le32(z, crc);
    le32(z, uint32_t(data.size())); le32(z, uint32_t(data.size())); le16(z, uint16_t(name.size())); le16(z, 0);
    z.insert(z.end(), name.begin(), name.end());
    z.insert(z.end(), data.begin(), data.end());
    const uint32_t cd = uint32_t(z.size());
    le32(z, 0x02014b50); le16(z, 20); le16(z, 20); le16(z, 0); le16(z, 0); le16(z, 0); le16(z, 0); le32(z, crc);
    le32(z, uint32_t(data.size())); le32(z, uint32_t(data.size())); le16(z, uint16_t(name.size())); le16(z, 0); le16(z, 0);
    le16(z, 0); le16(z, 0); le32(z, 0); le32(z, 0);
    z.insert(z.end(), name.begin(), name.end());
    const uint32_t cdSize = uint32_t(z.size()) - cd;
    le32(z, 0x06054b50); le16(z, 0); le16(z, 0); le16(z, 1); le16(z, 1); le32(z, cdSize); le32(z, cd); le16(z, 0);
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(z.data()), std::streamsize(z.size()));
}

// the same as a 7z file (libarchive writes it)
static bool write7z(const std::string& path, const std::string& name, const std::vector<uint8_t>& data) {
    archive* a = archive_write_new();
    archive_write_set_format_7zip(a);
    if (archive_write_open_filename(a, path.c_str()) != ARCHIVE_OK) { archive_write_free(a); return false; }
    archive_entry* e = archive_entry_new();
    archive_entry_set_pathname(e, name.c_str());
    archive_entry_set_size(e, la_int64_t(data.size()));
    archive_entry_set_filetype(e, AE_IFREG);
    archive_entry_set_perm(e, 0644);
    archive_write_header(a, e);
    archive_write_data(a, data.data(), data.size());
    archive_entry_free(e);
    archive_write_close(a);
    archive_write_free(a);
    return true;
}

static bool runOnce(const std::string& core, const std::string& rom, const fs::path& dir, bool full) {
    retro::Game g;
    g.core = core; g.rom = rom; g.title = "test";
    g.systemDir = (dir / "bios").string(); g.saveDir = (dir / "saves").string(); g.stateDir = (dir / "states").string();
    retro::setGame(g);
    retro::Stream s;
    std::string e;
    if (!s.open(e)) { std::printf("open: %s\n", e.c_str()); return false; }
    const int w = s.width(), h = s.height();
    std::printf("     %s, %dx%d, %.2f fps\n", retro::info().core.c_str(), w, h, s.fps());
    bool ok = w > 0 && h > 0 && w % 2 == 0 && h % 2 == 0 && s.fps() > 30;
    if (!full) { s.close(); return ok; }
    check(ok, "the picture size and rate");
    // sound in 5 ms blocks, as the capture audio thread asks for it
    std::atomic<bool> stop{false};
    std::atomic<long> loud{0};
    std::thread a([&] {
        std::vector<int16_t> buf(240 * 2);
        while (!stop) { retro::audio(buf.data(), 240); for (auto v : buf) if (v != 0) { ++loud; break; } }
    });
    int frames = 0;
    RawFrame f;
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(1))
        if (s.readLatest(f, 100, e) == retro::Stream::Result::Frame) ++frames;
    stop = true;
    a.join();
    std::printf("     %d pictures in 1 s, %ld blocks with sound\n", frames, loud.load());
    check(frames > 0.8 * s.fps() && frames < 1.2 * s.fps(), "pictures at the core's rate");
    check(f.w == w && f.h == h && f.data.size() == size_t(w) * h * 4, "RGBA pictures of the reported size");
    retro::saveState(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::string m = retro::takeMessage();
    check(m == "State saved" && retro::hasState(0), "save a state");
    retro::loadState(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    m = retro::takeMessage();
    check(m == "State loaded", "load the state");
    // run-ahead (2 frames) and rewind, switched on while the game runs: still in time, and stepping back works
    retro::setRunAhead(2);
    retro::setRewind(true);
    frames = 0;
    const auto t1 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t1 < std::chrono::seconds(1))
        if (s.readLatest(f, 100, e) == retro::Stream::Result::Frame) ++frames;
    std::printf("     run-ahead 2: %d pictures in 1 s\n", frames);
    check(frames > 0.8 * s.fps() && retro::takeMessage().empty(), "run-ahead keeps the core's rate");
    retro::setRewinding(true);
    frames = 0;
    const auto t2 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t2 < std::chrono::milliseconds(500))
        if (s.readLatest(f, 100, e) == retro::Stream::Result::Frame) ++frames;
    retro::setRewinding(false);
    retro::setRunAhead(0);
    retro::setRewind(false);
    check(frames > 0, "rewind shows pictures");
    s.close();
    check(!retro::running(), "closed");
    // quitting wrote the automatic state; "Continue" loads it
    check(fs::exists(retro::autoStatePath(g.stateDir, g.rom)), "the automatic state on quit");
    g.resume = true;
    retro::setGame(g);
    retro::Stream s2;
    check(s2.open(e), "opened again");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    check(retro::takeMessage() == "Continued where you left off", "continue where it was quit");
    s2.close();
    return true;
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--retro-host") == 0) return retro::hostMain();   // the child (second half)
    if (argc < 3) { std::fprintf(stderr, "usage: %s <core.so> <game>\n", argv[0]); return 2; }
    const fs::path dir = fs::temp_directory_path() / "veyamux-retro-test";
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::string name, exts;
    check(retro::coreLoads(argv[1], &name, &exts), "the core loads");
    std::printf("     %s reads %s\n", name.c_str(), exts.c_str());
    runOnce(argv[1], argv[2], dir, true);
    // the same game zipped (unpacked by VeyAmux unless the core reads zip files itself)
    std::ifstream in(argv[2], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), {});
    const std::string zip = (dir / "game.zip").string();
    writeZip(zip, fs::path(argv[2]).filename().string(), data);
    const auto list = retro::zipList(zip);
    check(list.size() == 1 && list[0].size == data.size(), "zip: the list");
    std::vector<uint8_t> back;
    std::string e;
    check(retro::zipRead(zip, list.empty() ? "" : list[0].name, back, e) && back == data, "zip: unpacked");
    check(runOnce(argv[1], zip, dir, false), "the zipped game runs");
    const std::string sz = (dir / "game.7z").string();
    check(write7z(sz, fs::path(argv[2]).filename().string(), data) && retro::zipList(sz).size() == 1, "7z: the list");
    check(runOnce(argv[1], sz, dir, false), "the game in a 7z file runs");
    // the same in a child process (as VeyAmux runs games): everything again, and an emulator that crashes stops its
    // own process only
    std::printf("     -- in a child process\n");
    retro::useHostProcess(fs::read_symlink("/proc/self/exe").string());
    fs::remove_all(dir);
    fs::create_directories(dir);
    runOnce(argv[1], argv[2], dir, true);
    if (argc >= 4) {
        retro::Game g;
        g.core = argv[3]; g.rom = (dir / "x.crash").string(); g.title = "crash";
        std::ofstream(g.rom) << "x";
        g.systemDir = g.saveDir = g.stateDir = dir.string();
        retro::setGame(g);
        retro::Stream s;
        std::string e;
        check(s.open(e), "the crashing emulator starts");
        RawFrame f;
        int frames = 0;
        retro::Stream::Result r = retro::Stream::Result::Timeout;
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(5)) {
            r = s.readLatest(f, 100, e);
            if (r == retro::Stream::Result::Frame) ++frames;
            if (r == retro::Stream::Result::Error) break;
        }
        std::printf("     %d pictures, then: %s\n", frames, e.c_str());
        check(frames > 10 && r == retro::Stream::Result::Error && e == "The emulator crashed", "a crash ends the game, not this process");
        s.close();
        // and a game after it runs
        check(runOnce(argv[1], argv[2], dir, false), "the next game runs after a crash");
    }
    // an OpenGL core (as N64 / Dreamcast / PSP emulators are): its picture arrives the right way up, and the input
    // reaches it (tests/gl_core.c: a red square in the bottom left corner, the rest green while B is held)
    if (argc >= 5) {
        retro::Game g;
        g.core = argv[4]; g.rom = (dir / "x.gltest").string(); g.title = "gl";
        std::ofstream(g.rom) << "x";
        g.systemDir = g.saveDir = g.stateDir = dir.string();
        retro::setGame(g);
        retro::Stream s;
        std::string e;
        const bool opened = s.open(e);
        check(opened, ("OpenGL core: opened " + e).c_str());
        if (opened) {
            RawFrame f;
            auto pixel = [&](int x, int y) { const uint8_t* p = f.data.data() + (size_t(y) * f.w + x) * 4; return std::array<int, 3>{p[0], p[1], p[2]}; };
            int frames = 0;
            const auto t0 = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(1))
                if (s.readLatest(f, 100, e) == retro::Stream::Result::Frame) ++frames;
            const auto bl = pixel(2, f.h - 3), tl = pixel(2, 2);
            std::printf("     %d pictures in 1 s, %dx%d, bottom left %d,%d,%d, top left %d,%d,%d\n", frames, f.w, f.h, bl[0], bl[1], bl[2], tl[0], tl[1], tl[2]);
            check(frames > 40 && bl[0] > 200 && bl[1] < 50 && tl[0] < 50 && tl[1] < 50, "OpenGL core: pictures, the right way up");
            retro::setPad(0, 1u << 0, 0, 0, 0, 0);   // B held
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            for (int i = 0; i < 5; ++i) s.readLatest(f, 100, e);
            const auto green = pixel(f.w / 2, 2);
            retro::setPad(0, 0, 0, 0, 0, 0);
            std::printf("     B held: top %d,%d,%d\n", green[0], green[1], green[2]);
            check(green[1] > 200 && green[0] < 50, "OpenGL core: the controller reaches it");
            s.close();
        }
    }
    // RetroAchievements signed in, the server out of reach (or answering no): the game runs and closes as without
    {
        retro::Game g;
        g.core = argv[1]; g.rom = argv[2]; g.title = "achievements";
        g.systemDir = (dir / "bios").string(); g.saveDir = (dir / "saves").string(); g.stateDir = (dir / "states").string();
        g.raUser = "veyamux-test"; g.raToken = "not-a-token"; g.raConsole = 4;
        retro::setGame(g);
        retro::Stream s;
        std::string e;
        check(s.open(e), "with RetroAchievements: opened");
        RawFrame f;
        int frames = 0;
        const auto t0 = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2))
            if (s.readLatest(f, 100, e) == retro::Stream::Result::Frame) ++frames;
        retro::saveState(1);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        s.close();
        std::printf("     %d pictures in 2 s, last message: %s\n", frames, retro::takeMessage().c_str());
        check(frames > 80 && !retro::running(), "with RetroAchievements: runs and closes");
    }
    fs::remove_all(dir);
    std::printf(failures ? "%d FAILED\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
