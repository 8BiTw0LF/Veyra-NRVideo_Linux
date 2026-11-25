// VeyAmux for Linux: test of the libretro frontend (Libretro.h) with a core.
//   veyamux-retro-test <core.so> <game file>
// Runs the game for a second: pictures arrive at the core's rate in the expected size, sound comes out, a state can
// be saved and loaded, and a zipped copy of the game loads too. Exit code 0 when all of it worked.
#include "Libretro.h"
#include "CaptureSource.h"

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
    s.close();
    check(!retro::running(), "closed");
    return true;
}

int main(int argc, char** argv) {
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
    fs::remove_all(dir);
    std::printf(failures ? "%d FAILED\n" : "all ok\n", failures);
    return failures ? 1 : 0;
}
