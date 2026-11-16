// VeyAmux for Linux: sound devices through libpulse (see PulseDevices.h).
#include "PulseDevices.h"

#include <pulse/pulseaudio.h>

#include <chrono>

namespace veyamux::pulse {

namespace {

struct Query {
    bool sinks = true;
    bool done = false;
    bool failed = false;
    std::vector<Device> out;
};

void onSink(pa_context*, const pa_sink_info* i, int eol, void* u) {
    auto* q = static_cast<Query*>(u);
    if (eol) { q->done = true; return; }
    if (i && i->name) q->out.push_back({i->name, i->description ? i->description : i->name, false});
}

void onSource(pa_context*, const pa_source_info* i, int eol, void* u) {
    auto* q = static_cast<Query*>(u);
    if (eol) { q->done = true; return; }
    if (i && i->name)
        q->out.push_back({i->name, i->description ? i->description : i->name, i->monitor_of_sink != PA_INVALID_INDEX});
}

std::vector<Device> list(bool sinks, int timeoutMs) {
    Query q;
    q.sinks = sinks;
    pa_mainloop* ml = pa_mainloop_new();
    if (!ml) return {};
    pa_proplist* props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "VeyAmux");
    pa_context* ctx = pa_context_new_with_proplist(pa_mainloop_get_api(ml), "VeyAmux", props);
    pa_proplist_free(props);
    if (!ctx || pa_context_connect(ctx, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) {
        if (ctx) pa_context_unref(ctx);
        pa_mainloop_free(ml);
        return {};
    }
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    pa_operation* op = nullptr;
    while (!q.done && !q.failed && std::chrono::steady_clock::now() < end) {
        // one loop round, waiting at most 50 ms for an event (so the timeout holds)
        if (pa_mainloop_prepare(ml, 50000) < 0 || pa_mainloop_poll(ml) < 0 || pa_mainloop_dispatch(ml) < 0) break;
        const pa_context_state_t st = pa_context_get_state(ctx);
        if (st == PA_CONTEXT_FAILED || st == PA_CONTEXT_TERMINATED) { q.failed = true; break; }
        if (st == PA_CONTEXT_READY && !op)
            op = sinks ? pa_context_get_sink_info_list(ctx, onSink, &q) : pa_context_get_source_info_list(ctx, onSource, &q);
    }
    if (op) { if (!q.done) pa_operation_cancel(op); pa_operation_unref(op); }
    pa_context_disconnect(ctx);
    pa_context_unref(ctx);
    pa_mainloop_free(ml);
    if (!q.done) return {};
    return q.out;
}

} // namespace

std::vector<Device> sinks(int timeoutMs) { return list(true, timeoutMs); }
std::vector<Device> sources(int timeoutMs) { return list(false, timeoutMs); }

} // namespace veyamux::pulse
