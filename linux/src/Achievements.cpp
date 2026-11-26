// VeyAmux for Linux: RetroAchievements in games. See Achievements.h.
#include "Achievements.h"

#include "../third_party/libretro/libretro.h"
#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_libretro.h"

#include <curl/curl.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace veyamux::retro::cheevos {

namespace {

std::mutex m;                       // guards `client` against the server calls' threads
rc_client_t* client = nullptr;
rc_libretro_memory_regions_t regions{};
std::function<void(const std::string&)> sayFn;
Start current;                      // (memoryData / memorySize are used by the memory regions callback)
std::string userAgent = "VeyAmux/2.1";

void say(const std::string& s) { if (sayFn) sayFn(s); }

void RC_CCONV logLine(const char* msg, const rc_client_t*) { std::fprintf(stderr, "Achievements: %s\n", msg); }

uint32_t RC_CCONV readMemory(uint32_t address, uint8_t* buffer, uint32_t n, rc_client_t*) {
    return rc_libretro_memory_read(&regions, address, buffer, n);
}

void RC_CCONV coreMemory(uint32_t id, rc_libretro_core_memory_info_t* info) {
    info->data = current.memoryData ? static_cast<uint8_t*>(current.memoryData(id)) : nullptr;
    info->size = current.memorySize ? current.memorySize(id) : 0;
}

size_t curlWrite(char* p, size_t s, size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, s * n);
    return s * n;
}

// a call to retroachievements.org, on a thread of its own; the answer goes back to rcheevos unless the game has
// ended meanwhile (then it is dropped)
void RC_CCONV serverCall(const rc_api_request_t* rq, rc_client_server_callback_t cb, void* cbData, rc_client_t* c) {
    std::string url = rq->url, post = rq->post_data ? rq->post_data : "", type = rq->content_type ? rq->content_type : "";
    std::thread([url, post, type, cb, cbData, c] {
        std::string body;
        long status = 0;
        if (CURL* h = curl_easy_init()) {
            curl_easy_setopt(h, CURLOPT_URL, url.c_str());
            curl_easy_setopt(h, CURLOPT_USERAGENT, userAgent.c_str());
            curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, curlWrite);
            curl_easy_setopt(h, CURLOPT_WRITEDATA, &body);
            curl_easy_setopt(h, CURLOPT_TIMEOUT, 20L);
            curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
            curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
            curl_slist* hdr = nullptr;
            if (!post.empty()) {
                curl_easy_setopt(h, CURLOPT_POSTFIELDS, post.c_str());
                if (!type.empty()) hdr = curl_slist_append(hdr, ("Content-Type: " + type).c_str());
                curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdr);
            }
            const CURLcode rc = curl_easy_perform(h);
            if (rc == CURLE_OK) curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
            else status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
            curl_slist_free_all(hdr);
            curl_easy_cleanup(h);
        } else {
            status = RC_API_SERVER_RESPONSE_CLIENT_ERROR;
        }
        rc_api_server_response_t res{};
        res.body = body.c_str();
        res.body_length = body.size();
        res.http_status_code = int(status);
        std::lock_guard l(m);
        if (client == c) cb(&res, cbData);
    }).detach();
}

void RC_CCONV onEvent(const rc_client_event_t* e, rc_client_t*) {
    switch (e->type) {
    case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED:
        if (e->achievement)
            say("Achievement unlocked: " + std::string(e->achievement->title) + " (" + std::to_string(e->achievement->points) + ")");
        break;
    case RC_CLIENT_EVENT_GAME_COMPLETED:
        say("All achievements in this game unlocked");
        break;
    case RC_CLIENT_EVENT_SERVER_ERROR:
        if (e->server_error) std::fprintf(stderr, "Achievements: server error: %s\n", e->server_error->error_message);
        break;
    case RC_CLIENT_EVENT_RESET:
        break;
    default:
        break;
    }
}

void RC_CCONV onGameLoaded(int result, const char* error, rc_client_t* c, void*) {
    if (result != RC_OK) {
        // no achievements for this game (its file is not one RetroAchievements knows), or no connection: said in the log only
        std::fprintf(stderr, "Achievements: none for this game (%s)\n", error ? error : rc_error_str(result));
        return;
    }
    rc_client_user_game_summary_t sum{};
    rc_client_get_user_game_summary(c, &sum);
    const rc_client_game_t* g = rc_client_get_game_info(c);
    std::fprintf(stderr, "Achievements: %s, %u of %u unlocked\n", g ? g->title : "?", sum.num_unlocked_achievements, sum.num_core_achievements);
    if (sum.num_core_achievements > 0)
        say("Achievements unlocked so far: " + std::to_string(sum.num_unlocked_achievements) + "/" + std::to_string(sum.num_core_achievements));
}

void RC_CCONV onLogin(int result, const char* error, rc_client_t* c, void*) {
    if (result != RC_OK) {
        std::fprintf(stderr, "Achievements: sign-in failed: %s\n", error ? error : rc_error_str(result));
        say("RetroAchievements sign-in failed: sign in again on the Games page");
        return;
    }
    rc_client_begin_identify_and_load_game(c, current.console, current.path.empty() ? nullptr : current.path.c_str(), current.data,
                                           current.size, onGameLoaded, nullptr);
}

} // namespace

uint32_t consoleFor(const std::string& s) {
    static const std::map<std::string, uint32_t> ids = {
        {"nes", RC_CONSOLE_NINTENDO}, {"fds", RC_CONSOLE_FAMICOM_DISK_SYSTEM}, {"snes", RC_CONSOLE_SUPER_NINTENDO},
        {"gb", RC_CONSOLE_GAMEBOY}, {"gbc", RC_CONSOLE_GAMEBOY_COLOR}, {"gba", RC_CONSOLE_GAMEBOY_ADVANCE},
        {"vb", RC_CONSOLE_VIRTUAL_BOY}, {"md", RC_CONSOLE_MEGA_DRIVE}, {"sms", RC_CONSOLE_MASTER_SYSTEM},
        {"gg", RC_CONSOLE_GAME_GEAR}, {"sg1000", RC_CONSOLE_SG1000}, {"segacd", RC_CONSOLE_SEGA_CD}, {"32x", RC_CONSOLE_SEGA_32X},
        {"saturn", RC_CONSOLE_SATURN}, {"pce", RC_CONSOLE_PC_ENGINE}, {"pcecd", RC_CONSOLE_PC_ENGINE_CD},
        {"psx", RC_CONSOLE_PLAYSTATION}, {"a2600", RC_CONSOLE_ATARI_2600}, {"a7800", RC_CONSOLE_ATARI_7800},
        {"lynx", RC_CONSOLE_ATARI_LYNX}, {"ngp", RC_CONSOLE_NEOGEO_POCKET}, {"ngpc", RC_CONSOLE_NEOGEO_POCKET},
        {"ws", RC_CONSOLE_WONDERSWAN}, {"wsc", RC_CONSOLE_WONDERSWAN}, {"arcade", RC_CONSOLE_ARCADE},
        {"n64", RC_CONSOLE_NINTENDO_64}, {"nds", RC_CONSOLE_NINTENDO_DS}, {"psp", RC_CONSOLE_PSP}, {"dc", RC_CONSOLE_DREAMCAST},
    };
    const auto it = ids.find(s);
    return it == ids.end() ? 0 : it->second;
}

void start(const Start& s) {
    stop();
    if (s.user.empty() || s.token.empty() || s.console == 0) return;
    current = s;
    sayFn = s.say;
    rc_libretro_memory_init(&regions, s.memoryMap, coreMemory, s.console);
    std::lock_guard l(m);
    client = rc_client_create(readMemory, serverCall);
    if (!client) return;
    char ua[128];
    if (rc_client_get_user_agent_clause(client, ua, sizeof ua) > 0) userAgent = std::string("VeyAmux/2.1 ") + ua;
    rc_client_enable_logging(client, RC_CLIENT_LOG_LEVEL_WARN, logLine);
    rc_client_set_event_handler(client, onEvent);
    rc_client_set_hardcore_enabled(client, 0);
    rc_client_begin_login_with_token(client, s.user.c_str(), s.token.c_str(), onLogin, nullptr);
}

void frame() { if (client) rc_client_do_frame(client); }
void idle() { if (client) rc_client_idle(client); }
void reset() { if (client) rc_client_reset(client); }
bool active() { return client && rc_client_is_game_loaded(client); }

void saveProgress(const std::string& statePath) {
    if (!active()) return;
    const size_t n = rc_client_progress_size(client);
    std::vector<uint8_t> d(n);
    if (n && rc_client_serialize_progress_sized(client, d.data(), n) == RC_OK)
        std::ofstream(statePath + ".achievements", std::ios::binary).write(reinterpret_cast<const char*>(d.data()), std::streamsize(n));
}

void loadProgress(const std::string& statePath) {
    if (!active()) return;
    std::ifstream f(statePath + ".achievements", std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    if (d.empty()) rc_client_deserialize_progress_sized(client, nullptr, 0);   // (a state from before: start the progress over)
    else rc_client_deserialize_progress_sized(client, d.data(), d.size());
}

void stop() {
    std::lock_guard l(m);
    if (client) {
        rc_client_unload_game(client);
        rc_client_destroy(client);
        client = nullptr;
    }
    rc_libretro_memory_destroy(&regions);
    regions = {};
}

} // namespace veyamux::retro::cheevos
