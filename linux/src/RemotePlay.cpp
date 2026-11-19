// VeyAmux for Linux: PlayStation Remote Play through chiaki-ng's library (see RemotePlay.h).
#include "RemotePlay.h"
#include "CaptureSource.h"

extern "C" {
#include <chiaki/base64.h>
#include <chiaki/common.h>
#include <chiaki/discovery.h>
#include <chiaki/discoveryservice.h>
#include <chiaki/log.h>
#include <chiaki/opusdecoder.h>
#include <chiaki/regist.h>
#include <chiaki/session.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}
#include <curl/curl.h>
#include <json-c/json.h>

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

namespace veyamux::remoteplay {

namespace {

std::once_flag g_init;
void libInit() {
    std::call_once(g_init, [] {
        chiaki_lib_init();
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

// chiaki's log: errors and warnings to stderr (VEYAMUX_PS_LOG=1: everything)
void logCb(ChiakiLogLevel level, const char* msg, void*) { std::fprintf(stderr, "Remote Play [%c] %s\n", chiaki_log_level_char(level), msg); }
void initLog(ChiakiLog* log) {
    const char* v = std::getenv("VEYAMUX_PS_LOG");
    chiaki_log_init(log, v && *v == '1' ? CHIAKI_LOG_ALL : (CHIAKI_LOG_ERROR | CHIAKI_LOG_WARNING), logCb, nullptr);
}

std::string toHex(const uint8_t* d, size_t n) {
    static const char* x = "0123456789abcdef";
    std::string s;
    for (size_t i = 0; i < n; ++i) { s += x[d[i] >> 4]; s += x[d[i] & 15]; }
    return s;
}
bool fromHex(const std::string& s, uint8_t* out, size_t n) {
    if (s.size() != n * 2) return false;
    for (size_t i = 0; i < n; ++i) {
        unsigned v = 0;
        if (std::sscanf(s.c_str() + i * 2, "%2x", &v) != 1) return false;
        out[i] = uint8_t(v);
    }
    return true;
}

// ------------------------------------------------------------------ discovery

std::mutex g_discM;
ChiakiLog g_discLog;
std::unique_ptr<ChiakiDiscoveryService> g_disc;
std::vector<Console> g_consoles;

void discoveryCb(ChiakiDiscoveryHost* hosts, size_t count, void*) {
    std::vector<Console> list;
    for (size_t i = 0; i < count; ++i) {
        ChiakiDiscoveryHost& h = hosts[i];
        Console c;
        c.id = h.host_id ? h.host_id : "";
        c.name = h.host_name ? h.host_name : "";
        c.host = h.host_addr ? h.host_addr : "";
        c.ps5 = chiaki_discovery_host_is_ps5(&h);
        c.ready = h.state == CHIAKI_DISCOVERY_HOST_STATE_READY;
        c.runningApp = h.running_app_name ? h.running_app_name : "";
        c.systemVersion = h.system_version ? h.system_version : "";
        if (!c.id.empty() && !c.host.empty()) list.push_back(std::move(c));
    }
    std::lock_guard l(g_discM);
    g_consoles = std::move(list);
}

} // namespace

void startDiscovery() {
    libInit();
    std::lock_guard l(g_discM);
    if (g_disc) return;
    initLog(&g_discLog);
    ChiakiDiscoveryServiceOptions o{};
    o.ping_ms = 500;
    o.hosts_max = 16;
    o.host_drop_pings = 3;
    o.cb = discoveryCb;
    sockaddr_in all{};
    all.sin_family = AF_INET;
    all.sin_addr.s_addr = 0xffffffff;
    sockaddr_storage addr{};
    std::memcpy(&addr, &all, sizeof all);
    o.send_addr = &addr;
    o.send_addr_size = sizeof all;
    // every network's own broadcast address too (255.255.255.255 does not leave every interface)
    std::vector<sockaddr_storage> bc;
    ifaddrs* ifs = nullptr;
    if (getifaddrs(&ifs) == 0) {
        for (ifaddrs* a = ifs; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !a->ifa_broadaddr) continue;
            if ((a->ifa_flags & (IFF_UP | IFF_RUNNING | IFF_LOOPBACK | IFF_BROADCAST)) != (IFF_UP | IFF_RUNNING | IFF_BROADCAST)) continue;
            sockaddr_storage s{};
            std::memcpy(&s, a->ifa_broadaddr, sizeof(sockaddr_in));
            bc.push_back(s);
        }
        freeifaddrs(ifs);
    }
    o.broadcast_addrs = bc.empty() ? nullptr : bc.data();
    o.broadcast_num = bc.size();
    auto svc = std::make_unique<ChiakiDiscoveryService>();
    if (chiaki_discovery_service_init(svc.get(), &o, &g_discLog) == CHIAKI_ERR_SUCCESS) g_disc = std::move(svc);
}

void stopDiscovery() {
    std::unique_ptr<ChiakiDiscoveryService> svc;
    { std::lock_guard l(g_discM); svc = std::move(g_disc); }
    if (svc) chiaki_discovery_service_fini(svc.get());
}

std::vector<Console> consoles() {
    std::lock_guard l(g_discM);
    return g_consoles;
}

// ------------------------------------------------------------------ PSN account ID

namespace {
const char* kClientId = "ba495a24-818c-472b-b12d-ff231c1b5745";
const char* kClientSecret = "mvaiZkRsAsI1IBkY";
const char* kRedirect = "https://remoteplay.dl.playstation.net/remoteplay/redirect";
const char* kTokenUrl = "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/token";
const char* kScope = "psn:clientapp referenceDataService:countryConfig.read pushNotification:webSocket.desktop.connect "
                     "sessionManager:remotePlaySession.system.update";

size_t curlWrite(char* p, size_t s, size_t n, void* u) { static_cast<std::string*>(u)->append(p, s * n); return s * n; }

bool httpRequest(const std::string& url, const std::string* postBody, std::string& reply, std::string& error) {
    CURL* c = curl_easy_init();
    if (!c) { error = "Network unavailable"; return false; }
    curl_slist* h = nullptr;
    h = curl_slist_append(h, "Accept: application/json");
    if (postBody) h = curl_slist_append(h, "Content-Type: application/x-www-form-urlencoded");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
    curl_easy_setopt(c, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(c, CURLOPT_USERNAME, kClientId);
    curl_easy_setopt(c, CURLOPT_PASSWORD, kClientSecret);
    if (postBody) curl_easy_setopt(c, CURLOPT_POSTFIELDS, postBody->c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWrite);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 20L);
    // the AppImage carries Ubuntu's libcurl, which looks for Debian's certificate file; Fedora / Bazzite / Arch keep it
    // elsewhere
    for (const char* ca : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
                           "/etc/ssl/ca-bundle.pem", "/etc/ssl/cert.pem"})
        if (access(ca, R_OK) == 0) { curl_easy_setopt(c, CURLOPT_CAINFO, ca); break; }
    const CURLcode r = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(h);
    curl_easy_cleanup(c);
    if (r != CURLE_OK) { error = curl_easy_strerror(r); return false; }
    if (code != 200) { error = "PlayStation Network answered " + std::to_string(code); return false; }
    return true;
}

std::string jsonString(const std::string& text, const char* key) {
    std::string v;
    if (json_object* o = json_tokener_parse(text.c_str())) {
        json_object* f = nullptr;
        if (json_object_object_get_ex(o, key, &f)) v = json_object_get_string(f);
        json_object_put(o);
    }
    return v;
}
} // namespace

std::string psnLoginUrl() {
    std::string s = "https://auth.api.sonyentertainmentnetwork.com/2.0/oauth/authorize?service_entity=urn:service-entity:psn"
                    "&response_type=code&client_id=";
    s += kClientId;
    s += "&redirect_uri=";
    s += kRedirect;
    s += "&scope=psn:clientapp%20referenceDataService:countryConfig.read%20pushNotification:webSocket.desktop.connect"
         "%20sessionManager:remotePlaySession.system.update&request_locale=en_US&ui=pr&service_logo=ps"
         "&layout_type=popup&smcid=remoteplay&prompt=always&PlatformPrivacyWs1=minimal&";
    return s;
}

bool psnAccountId(const std::string& redirectUrl, std::string& accountId, std::string& error) {
    libInit();
    // the code: ...redirect?code=XXXX&cid=...
    const size_t at = redirectUrl.find("code=");
    if (at == std::string::npos) { error = "That address has no login code (it should contain \"code=\")"; return false; }
    std::string code = redirectUrl.substr(at + 5);
    if (const size_t amp = code.find('&'); amp != std::string::npos) code.resize(amp);
    while (!code.empty() && std::isspace(uint8_t(code.back()))) code.pop_back();
    std::string body = std::string("grant_type=authorization_code&code=") + code + "&scope=" + kScope + "&redirect_uri=" + kRedirect + "&";
    std::string reply;
    if (!httpRequest(kTokenUrl, &body, reply, error)) return false;
    const std::string token = jsonString(reply, "access_token");
    if (token.empty()) { error = "No access token from PlayStation Network (the code is used once: log in again)"; return false; }
    reply.clear();
    if (!httpRequest(std::string(kTokenUrl) + "/" + token, nullptr, reply, error)) return false;
    const std::string user = jsonString(reply, "user_id");
    if (user.empty()) { error = "PlayStation Network sent no account number"; return false; }
    // the account ID is the 64-bit user id, little endian, base64
    unsigned long long v = std::strtoull(user.c_str(), nullptr, 10);
    uint8_t le[8];
    for (int i = 0; i < 8; ++i) { le[i] = uint8_t(v & 0xff); v >>= 8; }
    char b64[32]{};
    if (chiaki_base64_encode(le, 8, b64, sizeof b64) != CHIAKI_ERR_SUCCESS) { error = "Encoding failed"; return false; }
    accountId = b64;
    return true;
}

// ------------------------------------------------------------------ registration / wake

bool registerConsole(const Console& c, const std::string& accountIdBase64, uint32_t pin, Registered& out, std::string& error) {
    libInit();
    uint8_t acc[CHIAKI_PSN_ACCOUNT_ID_SIZE]{};
    size_t accSize = sizeof acc;
    if (chiaki_base64_decode(accountIdBase64.c_str(), accountIdBase64.size(), acc, &accSize) != CHIAKI_ERR_SUCCESS || accSize != sizeof acc) {
        error = "The PSN account ID is not valid";
        return false;
    }
    // the console type and firmware decide the protocol version
    ChiakiTarget target = c.ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_10;
    if (!c.systemVersion.empty()) {
        ChiakiDiscoveryHost h{};
        h.system_version = c.systemVersion.c_str();
        h.host_type = c.ps5 ? "PS5" : "PS4";
        h.device_discovery_protocol_version = c.ps5 ? CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5 : CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4;
        const ChiakiTarget t = chiaki_discovery_host_system_version_target(&h);
        if (!chiaki_target_is_unknown(t)) target = t;
    }
    ChiakiRegistInfo info{};
    info.target = target;
    info.host = c.host.c_str();
    info.broadcast = false;
    info.psn_online_id = nullptr;
    std::memcpy(info.psn_account_id, acc, sizeof acc);
    info.pin = pin;
    info.console_pin = 0;
    struct Wait {
        std::mutex m;
        std::condition_variable cv;
        bool done = false, ok = false;
        ChiakiRegisteredHost host{};
    } w;
    ChiakiLog log;
    initLog(&log);
    ChiakiRegist regist{};
    auto cb = [](ChiakiRegistEvent* e, void* user) {
        auto* w = static_cast<Wait*>(user);
        std::lock_guard l(w->m);
        w->ok = e->type == CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS && e->registered_host;
        if (w->ok) w->host = *e->registered_host;
        w->done = true;
        w->cv.notify_all();
    };
    if (chiaki_regist_start(&regist, &log, &info, cb, &w) != CHIAKI_ERR_SUCCESS) { error = "Could not start the registration"; return false; }
    {
        std::unique_lock l(w.m);
        if (!w.cv.wait_for(l, std::chrono::seconds(40), [&] { return w.done; })) {
            l.unlock();
            chiaki_regist_stop(&regist);
            l.lock();
            w.cv.wait_for(l, std::chrono::seconds(5), [&] { return w.done; });
        }
    }
    chiaki_regist_fini(&regist);
    if (!w.ok) {
        error = "Registration failed: check the PIN (it changes every few minutes) and that the console shows the "
                "\"Link Device\" screen";
        return false;
    }
    out.id = c.id;
    out.name = w.host.server_nickname[0] ? std::string(w.host.server_nickname) : c.name;
    out.host = c.host;
    out.ps5 = chiaki_target_is_ps5(w.host.target);
    out.target = int(w.host.target);
    out.registKey = toHex(reinterpret_cast<const uint8_t*>(w.host.rp_regist_key), sizeof w.host.rp_regist_key);
    out.morning = toHex(w.host.rp_key, sizeof w.host.rp_key);
    return true;
}

bool wake(const Registered& r, std::string& error) {
    libInit();
    uint8_t key[CHIAKI_SESSION_AUTH_SIZE]{};
    if (!fromHex(r.registKey, key, sizeof key)) { error = "The console's key is damaged: register it again"; return false; }
    // the regist key holds hex digits; the wake-up credential is their value
    std::string digits(reinterpret_cast<const char*>(key), strnlen(reinterpret_cast<const char*>(key), sizeof key));
    const uint64_t credential = std::strtoull(digits.c_str(), nullptr, 16);
    ChiakiLog log;
    initLog(&log);
    if (chiaki_discovery_wakeup(&log, nullptr, r.host.c_str(), credential, r.ps5) != CHIAKI_ERR_SUCCESS) {
        error = "Could not send the wake-up";
        return false;
    }
    return true;
}

// ------------------------------------------------------------------ streaming

namespace {
std::mutex g_cfgM;
StreamConfig g_cfg;
std::function<void(int, int)> g_rumble;
}

void setStreamConfig(const StreamConfig& cfg) { std::lock_guard l(g_cfgM); g_cfg = cfg; }
void setRumbleCallback(std::function<void(int, int)> cb) { std::lock_guard l(g_cfgM); g_rumble = std::move(cb); }

struct Stream::Impl {
    StreamConfig cfg;
    ChiakiLog log;
    ChiakiSession session{};
    ChiakiOpusDecoder opus{};
    bool sessionInit = false, opusInit = false, started = false;
    std::string hostStr;

    AVCodecContext* dec = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    AVFrame* swFrame = nullptr;          // a VA-API picture copied to memory (NV12 / P010)
    AVBufferRef* hwDevice = nullptr;
    const AVCodec* codec = nullptr;
    std::atomic<bool> hwActive{false};   // decoding on the graphics card (VA-API)
    bool hwFailed = false;
    RawFrame spare;                      // the buffer of the last picture handed out, reused for the next one

    std::mutex m;
    std::condition_variable cv;
    RawFrame latest;
    bool fresh = false;
    bool quit = false;
    std::string quitReason;
    bool pinSent = false;

    std::mutex audioM;
    std::deque<int16_t> audio;   // interleaved stereo, 48 kHz
    int channels = 2;

    std::mutex ctrlM;
    ChiakiControllerState ctrl{};

    std::atomic<double> decodeMs{0};
    std::atomic<uint64_t> framesLost{0};

    ~Impl() { stop(); }

    void stop() {
        if (started) { chiaki_session_stop(&session); chiaki_session_join(&session); started = false; }
        if (sessionInit) { chiaki_session_fini(&session); sessionInit = false; }
        if (opusInit) { chiaki_opus_decoder_fini(&opus); opusInit = false; }
        if (dec) avcodec_free_context(&dec);
        if (pkt) av_packet_free(&pkt);
        if (frame) av_frame_free(&frame);
        if (swFrame) av_frame_free(&swFrame);
        if (hwDevice) av_buffer_unref(&hwDevice);
    }

    static AVPixelFormat pickVaapi(AVCodecContext*, const AVPixelFormat* fmts) {
        for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) if (*p == AV_PIX_FMT_VAAPI) return *p;
        for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
            const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(*p);
            if (d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *p;
        }
        return AV_PIX_FMT_NONE;
    }

    // The decoder: VA-API on the graphics card when it can (much less CPU, important on a Steam Deck), else FFmpeg's
    // software decoder with slice threads. VEYAMUX_PS_SWDECODE=1 forces software.
    bool openDecoder(bool tryHw, std::string& error) {
        if (dec) avcodec_free_context(&dec);
        dec = avcodec_alloc_context3(codec);
        dec->flags |= AV_CODEC_FLAG_LOW_DELAY;
        dec->thread_type = FF_THREAD_SLICE;   // frame threads would add frames of delay
        dec->thread_count = 0;
        hwActive = false;
        if (tryHw && !std::getenv("VEYAMUX_PS_SWDECODE")) {
            if (!hwDevice) {
                const char* dev = std::getenv("VEYAMUX_VAAPI_DEVICE");
                if (av_hwdevice_ctx_create(&hwDevice, AV_HWDEVICE_TYPE_VAAPI, dev, nullptr, 0) < 0) hwDevice = nullptr;
            }
            if (hwDevice) {
                dec->hw_device_ctx = av_buffer_ref(hwDevice);
                dec->get_format = pickVaapi;
                dec->extra_hw_frames = 2;
            }
        }
        if (avcodec_open2(dec, codec, nullptr) < 0) {
            if (tryHw && hwDevice) return openDecoder(false, error);
            error = "Could not open the video decoder";
            return false;
        }
        return true;
    }

    static bool videoCb(uint8_t* buf, size_t size, int32_t framesLost, bool, void* user) {
        auto* s = static_cast<Impl*>(user);
        if (framesLost > 0) s->framesLost += uint64_t(framesLost);
        return s->decode(buf, size);
    }

    bool decode(uint8_t* buf, size_t size) {
        const auto t0 = std::chrono::steady_clock::now();
        pkt->data = buf;
        pkt->size = int(size);
        if (avcodec_send_packet(dec, pkt) < 0) return false;   // false: chiaki asks the console for a new key frame
        while (avcodec_receive_frame(dec, frame) == 0) {
            const AVFrame* use = frame;
            if (frame->format == AV_PIX_FMT_VAAPI) {
                av_frame_unref(swFrame);
                if (av_hwframe_transfer_data(swFrame, frame, 0) < 0) {
                    // the graphics card cannot hand the picture over: software decoding from the next key frame on
                    av_frame_unref(frame);
                    std::string e;
                    if (!hwFailed) { hwFailed = true; openDecoder(false, e); }
                    return false;
                }
                hwActive = true;
                use = swFrame;
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            const double o = decodeMs.load();
            decodeMs = o <= 0 ? ms : o + 0.1 * (ms - o);
            deliver(use);
            av_frame_unref(frame);
        }
        return true;
    }

    // -> NV12 / P010 (what VeyAmux's GPU path converts): VA-API already gives NV12 / P010 (rows are copied), the
    // software decoder gives planar YUV 4:2:0 (U and V are interleaved here)
    void deliver(const AVFrame* frame) {
        const int w = frame->width, h = frame->height;
        const bool semi = frame->format == AV_PIX_FMT_NV12 || frame->format == AV_PIX_FMT_P010LE;
        const bool ten = frame->format == AV_PIX_FMT_YUV420P10LE || frame->format == AV_PIX_FMT_P010LE;
        if (!semi && !ten && frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) return;
        RawFrame f;
        { std::lock_guard l(m); f.data.swap(spare.data); }   // reuse a buffer: no 3-6 MB allocation per picture
        f.fmt = ten ? RawFrame::P010 : RawFrame::NV12;
        f.w = w; f.h = h;
        const int bpp = ten ? 2 : 1;
        f.stride = w * bpp;
        f.data.resize(size_t(f.stride) * h * 3 / 2);
        uint8_t* y = f.data.data();
        uint8_t* uv = y + size_t(f.stride) * h;
        if (semi) {
            const size_t row = size_t(w) * bpp;
            for (int r = 0; r < h; ++r) std::memcpy(y + size_t(r) * f.stride, frame->data[0] + size_t(r) * frame->linesize[0], row);
            for (int r = 0; r < h / 2; ++r) std::memcpy(uv + size_t(r) * f.stride, frame->data[1] + size_t(r) * frame->linesize[1], row);
        } else if (!ten) {
            for (int r = 0; r < h; ++r) std::memcpy(y + size_t(r) * w, frame->data[0] + size_t(r) * frame->linesize[0], w);
            for (int r = 0; r < h / 2; ++r) {
                const uint8_t* u = frame->data[1] + size_t(r) * frame->linesize[1];
                const uint8_t* v = frame->data[2] + size_t(r) * frame->linesize[2];
                uint8_t* d = uv + size_t(r) * w;
                for (int x = 0; x < w / 2; ++x) { d[2 * x] = u[x]; d[2 * x + 1] = v[x]; }
            }
        } else {   // 10 bits in the low bits -> P010 (high bits)
            for (int r = 0; r < h; ++r) {
                const uint16_t* s = reinterpret_cast<const uint16_t*>(frame->data[0] + size_t(r) * frame->linesize[0]);
                uint16_t* d = reinterpret_cast<uint16_t*>(y + size_t(r) * f.stride);
                for (int x = 0; x < w; ++x) d[x] = uint16_t(s[x] << 6);
            }
            for (int r = 0; r < h / 2; ++r) {
                const uint16_t* u = reinterpret_cast<const uint16_t*>(frame->data[1] + size_t(r) * frame->linesize[1]);
                const uint16_t* v = reinterpret_cast<const uint16_t*>(frame->data[2] + size_t(r) * frame->linesize[2]);
                uint16_t* d = reinterpret_cast<uint16_t*>(uv + size_t(r) * f.stride);
                for (int x = 0; x < w / 2; ++x) { d[2 * x] = uint16_t(u[x] << 6); d[2 * x + 1] = uint16_t(v[x] << 6); }
            }
        }
        f.captured = std::chrono::steady_clock::now();
        std::lock_guard l(m);
        std::swap(latest, f);
        if (f.data.capacity() > spare.data.capacity()) spare.data.swap(f.data);   // an unread older picture: reuse it
        fresh = true;
        cv.notify_all();
    }

    static void eventCb(ChiakiEvent* e, void* user) {
        auto* s = static_cast<Impl*>(user);
        switch (e->type) {
        case CHIAKI_EVENT_QUIT: {
            std::lock_guard l(s->m);
            s->quit = true;
            if (e->quit.reason == CHIAKI_QUIT_REASON_STOPPED) s->quitReason = "Remote Play stopped";
            else if (e->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE) s->quitReason = "Remote Play is already in use on the console";
            else if (e->quit.reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED) s->quitReason = "The console refused the connection (is Remote Play enabled?)";
            else if (e->quit.reason == CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN) s->quitReason = "The console was turned off";
            else s->quitReason = std::string("Remote Play ended: ") + chiaki_quit_reason_string(e->quit.reason) +
                                 (e->quit.reason_str ? std::string(" (") + e->quit.reason_str + ")" : "");
            s->cv.notify_all();
            break;
        }
        case CHIAKI_EVENT_LOGIN_PIN_REQUEST: {
            const std::string& pc = s->cfg.passcode;
            if (!pc.empty() && !e->login_pin_request.pin_incorrect && !s->pinSent) {
                s->pinSent = true;
                chiaki_session_set_login_pin(&s->session, reinterpret_cast<const uint8_t*>(pc.data()), pc.size());
            } else {
                std::lock_guard l(s->m);
                s->quit = true;
                s->quitReason = pc.empty() ? "The console asks for its login passcode: enter it in the Remote Play window"
                                           : "The console's login passcode is wrong";
                s->cv.notify_all();
            }
            break;
        }
        case CHIAKI_EVENT_RUMBLE: {
            std::function<void(int, int)> cb;
            { std::lock_guard l(g_cfgM); cb = g_rumble; }
            if (cb) cb(e->rumble.left, e->rumble.right);
            break;
        }
        default:
            break;
        }
    }

    static void audioSettingsCb(uint32_t channels, uint32_t, void* user) { static_cast<Impl*>(user)->channels = int(std::max(1u, channels)); }
    static void audioFrameCb(int16_t* buf, size_t samples, void* user) {
        auto* s = static_cast<Impl*>(user);
        std::lock_guard l(s->audioM);
        const int ch = s->channels;
        for (size_t i = 0; i < samples; ++i) {
            const int16_t left = buf[i * ch], right = ch > 1 ? buf[i * ch + 1] : left;
            s->audio.push_back(left);
            s->audio.push_back(right);
        }
        // keep at most ~120 ms queued: the console's clock and the sound card's drift apart
        const size_t maxSamples = 48000 * 2 * 120 / 1000;
        if (s->audio.size() > maxSamples) s->audio.erase(s->audio.begin(), s->audio.begin() + long(s->audio.size() - maxSamples / 2));
    }
};

namespace {
std::mutex g_activeM;
Stream::Impl* g_active = nullptr;
}

Stream::Stream() = default;
Stream::~Stream() { close(); }

bool Stream::open(const std::string& path, std::string& error) {
    close();
    libInit();
    auto p = std::make_unique<Impl>();
    { std::lock_guard l(g_cfgM); p->cfg = g_cfg; }
    const std::string id = path.rfind("ps:", 0) == 0 ? path.substr(3) : path;
    if (p->cfg.console.id != id) { error = "Choose the PlayStation in the Remote Play window first"; return false; }
    initLog(&p->log);
    const bool ps5 = p->cfg.console.ps5;
    const bool hdr = ps5 && p->cfg.hdr;

    p->codec = avcodec_find_decoder(ps5 ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    if (!p->codec) { error = "This FFmpeg has no H.264/H.265 decoder"; return false; }
    if (!p->openDecoder(true, error)) return false;
    p->pkt = av_packet_alloc();
    p->frame = av_frame_alloc();
    p->swFrame = av_frame_alloc();

    ChiakiConnectInfo ci{};
    p->hostStr = p->cfg.console.host;
    ci.ps5 = ps5;
    ci.host = p->hostStr.c_str();
    uint8_t key[CHIAKI_SESSION_AUTH_SIZE]{};
    if (!fromHex(p->cfg.console.registKey, key, sizeof key) || !fromHex(p->cfg.console.morning, ci.morning, sizeof ci.morning)) {
        error = "The console's key is damaged: register it again";
        return false;
    }
    std::memcpy(ci.regist_key, key, sizeof ci.regist_key);
    chiaki_connect_video_profile_preset(&ci.video_profile,
                                        p->cfg.height <= 720 ? CHIAKI_VIDEO_RESOLUTION_PRESET_720p : CHIAKI_VIDEO_RESOLUTION_PRESET_1080p,
                                        p->cfg.fps <= 30 ? CHIAKI_VIDEO_FPS_PRESET_30 : CHIAKI_VIDEO_FPS_PRESET_60);
    if (ps5) {
        ci.video_profile.codec = hdr ? CHIAKI_CODEC_H265_HDR : CHIAKI_CODEC_H265;
        if (ci.video_profile.height >= 1080) ci.video_profile.bitrate = 30000;
    }
    ci.video_profile_auto_downgrade = true;
    ci.enable_keyboard = false;
    ci.enable_dualsense = false;
    ci.enable_idr_on_fec_failure = true;
    ci.packet_loss_max = 0.05;
    ci.auto_regist = false;
    ci.holepunch_session = nullptr;
    ci.rudp_sock = nullptr;
    if (chiaki_session_init(&p->session, &ci, &p->log) != CHIAKI_ERR_SUCCESS) { error = "Could not start Remote Play"; return false; }
    p->sessionInit = true;
    chiaki_opus_decoder_init(&p->opus, &p->log);
    p->opusInit = true;
    chiaki_opus_decoder_set_cb(&p->opus, Impl::audioSettingsCb, Impl::audioFrameCb, p.get());
    ChiakiAudioSink sink;
    chiaki_opus_decoder_get_sink(&p->opus, &sink);
    chiaki_session_set_audio_sink(&p->session, &sink);
    chiaki_session_set_video_sample_cb(&p->session, Impl::videoCb, p.get());
    chiaki_session_set_event_cb(&p->session, Impl::eventCb, p.get());
    chiaki_controller_state_set_idle(&p->ctrl);
    if (chiaki_session_start(&p->session) != CHIAKI_ERR_SUCCESS) { error = "Could not connect to the console"; return false; }
    p->started = true;
    p_ = std::move(p);
    std::lock_guard l(g_activeM);
    g_active = p_.get();
    return true;
}

void Stream::close() {
    { std::lock_guard l(g_activeM); if (g_active == p_.get()) g_active = nullptr; }
    p_.reset();
}

Stream::Result Stream::readLatest(RawFrame& out, int timeoutMs, std::string& error) {
    if (!p_) { error = "Remote Play is closed"; return Result::Error; }
    std::unique_lock l(p_->m);
    p_->cv.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return p_->fresh || p_->quit; });
    if (p_->fresh) {
        std::swap(out, p_->latest);
        p_->fresh = false;
        if (p_->latest.data.capacity() > p_->spare.data.capacity()) p_->spare.data.swap(p_->latest.data);   // reused next time
        return Result::Frame;
    }
    if (p_->quit) { error = p_->quitReason; return Result::Error; }
    return Result::Timeout;
}

int Stream::width() const { return p_ && p_->cfg.height <= 720 ? 1280 : 1920; }
int Stream::height() const { return p_ && p_->cfg.height <= 720 ? 720 : 1080; }
double Stream::fps() const { return p_ ? (p_->cfg.fps <= 30 ? 30.0 : 60.0) : 60.0; }
std::string Stream::name() const { return p_ ? p_->cfg.console.name + " (Remote Play)" : "Remote Play"; }

bool active() { std::lock_guard l(g_activeM); return g_active != nullptr; }

DecodeTest decodeTest(const std::string& file, bool hevc) {
    DecodeTest t;
    FILE* f = std::fopen(file.c_str(), "rb");
    if (!f) { t.error = "cannot open " + file; return t; }
    Stream::Impl s;
    s.codec = avcodec_find_decoder(hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264);
    if (!s.codec || !s.openDecoder(true, t.error)) { std::fclose(f); return t; }
    s.pkt = av_packet_alloc();
    s.frame = av_frame_alloc();
    s.swFrame = av_frame_alloc();
    AVCodecParserContext* parser = av_parser_init(s.codec->id);
    std::vector<uint8_t> in(1 << 16);
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        const size_t n = std::fread(in.data(), 1, in.size(), f);
        const uint8_t* d = in.data();
        size_t left = n;
        while (left > 0 || n == 0) {
            uint8_t* out = nullptr;
            int outSize = 0;
            const int used = av_parser_parse2(parser, s.dec, &out, &outSize, d, int(left), AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            d += used; left -= size_t(used);
            if (outSize > 0) {
                std::vector<uint8_t> unit(out, out + outSize);   // chiaki hands over a writable buffer, as here
                s.decode(unit.data(), unit.size());
                std::lock_guard l(s.m);
                if (s.fresh) {
                    ++t.frames;
                    t.w = s.latest.w; t.h = s.latest.h;
                    t.p010 = s.latest.fmt == RawFrame::P010;
                    if (t.frames == 1) t.firstLuma = s.latest.data.empty() ? 0 : s.latest.data[size_t(s.latest.stride) * (s.latest.h / 2) + size_t(s.latest.w / 2) * (t.p010 ? 2 : 1) + (t.p010 ? 1 : 0)];
                    s.fresh = false;
                }
            }
            if (n == 0) break;
        }
        if (n == 0) break;
    }
    t.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    t.hw = s.hwActive.load();
    av_parser_close(parser);
    std::fclose(f);
    return t;
}

Stats stats() {
    std::lock_guard l(g_activeM);
    Stats st;
    Stream::Impl* s = g_active;
    if (!s || !s->started) return st;
    st.active = true;
    st.rttMs = double(s->session.rtt_us) / 1000.0;
    st.decodeMs = s->decodeMs.load();
    st.hwDecode = s->hwActive.load();
    st.mbit = s->session.stream_connection.measured_bitrate;
    st.loss = s->session.stream_connection.congestion_control.packet_loss;
    st.framesLost = s->framesLost.load();
    st.ps5 = s->cfg.console.ps5;
    st.hdr = s->cfg.console.ps5 && s->cfg.hdr;
    return st;
}

void setController(uint32_t buttons, double lx, double ly, double rx, double ry, double l2, double r2) {
    std::lock_guard l(g_activeM);
    Stream::Impl* s = g_active;
    if (!s || !s->started) return;
    auto axis = [](double v) { return int16_t(std::lround(std::clamp(v, -1.0, 1.0) * 32767.0)); };
    std::lock_guard c(s->ctrlM);
    ChiakiControllerState st = s->ctrl;
    st.buttons = buttons;
    st.left_x = axis(lx); st.left_y = axis(ly);
    st.right_x = axis(rx); st.right_y = axis(ry);
    st.l2_state = uint8_t(std::lround(std::clamp(l2, 0.0, 1.0) * 255.0));
    st.r2_state = uint8_t(std::lround(std::clamp(r2, 0.0, 1.0) * 255.0));
    if (chiaki_controller_state_equals(&st, &s->ctrl)) return;
    s->ctrl = st;
    chiaki_session_set_controller_state(&s->session, &st);
}

void audio(int16_t* out, int frames) {
    std::lock_guard l(g_activeM);
    Stream::Impl* s = g_active;
    const size_t n = size_t(frames) * 2;
    if (!s) { std::fill(out, out + n, 0); return; }
    std::lock_guard a(s->audioM);
    size_t i = 0;
    for (; i < n && !s->audio.empty(); ++i) { out[i] = s->audio.front(); s->audio.pop_front(); }
    std::fill(out + i, out + n, 0);
}

} // namespace veyamux::remoteplay
