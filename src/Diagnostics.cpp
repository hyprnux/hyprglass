#include "Diagnostics.hpp"
#include "Globals.hpp"

#include <array>
#include <cstdio>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>

#include <EGL/egl.h>
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#include <hyprland/src/helpers/MiscFunctions.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorState.hpp>

namespace Diagnostics {

namespace {

std::unordered_map<MONITORID, SMonitorCounters> s_counters;

SMonitorCounters& countersFor(MONITORID monitor) {
    return s_counters[monitor];
}

// A few frames of slack per stage so a query that isn't ready yet doesn't
// force us to either block or drop a frame's sample.
constexpr int QUERY_RING_SIZE = 3;

struct SStageQueryRing {
    std::array<GLuint, QUERY_RING_SIZE>    queries{};
    std::array<bool, QUERY_RING_SIZE>      pending{};
    // The monitor being rendered when each pending slot's query was opened
    // (captured in CScopedStageTimer's constructor). A query's result is only
    // ever readable well after glEndQuery, on a later call to drainStage(), by
    // which point g_pHyprRenderer->m_renderData.pMonitor may have moved on to
    // another monitor entirely — the result must carry its own monitor id
    // rather than being attributed to whatever is current at drain time.
    std::array<MONITORID, QUERY_RING_SIZE> monitor{};
    int                                    nextSlot = 0;
};

constexpr size_t STAGE_COUNT = static_cast<size_t>(EStage::Count);

std::array<SStageQueryRing, STAGE_COUNT> s_queryRings;
// Stage nanoseconds, keyed by monitor like SMonitorCounters — a monitor's
// stage cost must not be blended into another monitor's average.
std::unordered_map<MONITORID, std::array<uint64_t, STAGE_COUNT>> s_stageNanoseconds;

constexpr std::array<std::string_view, STAGE_COUNT> STAGE_NAMES = {
    "sample_background",
    "blur_background",
    "apply_glass_effect",
    "layer_sample",
    "layer_composite",
};

// Only one GL_TIME_ELAPSED query may be open across the whole GL context at
// once. A bracket opened while this is true silently no-ops rather than
// nesting, which GL forbids outright.
bool s_queryActive = false;

bool                             s_extensionChecked   = false;
bool                             s_extensionAvailable = false;
PFNGLGETQUERYOBJECTUI64VEXTPROC  s_glGetQueryObjectui64vEXT = nullptr;

bool timerExtensionAvailable() {
    if (s_extensionChecked)
        return s_extensionAvailable;
    s_extensionChecked = true;

    const auto* extensions = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
    if (extensions && std::string_view(extensions).find("GL_EXT_disjoint_timer_query") != std::string_view::npos) {
        s_glGetQueryObjectui64vEXT =
            reinterpret_cast<PFNGLGETQUERYOBJECTUI64VEXTPROC>(eglGetProcAddress("glGetQueryObjectui64vEXT"));
        s_extensionAvailable = s_glGetQueryObjectui64vEXT != nullptr;
    }

    return s_extensionAvailable;
}

// Folds every finished query in a stage's ring into its running total.
// Never blocks: only reads results GL already reports as available.
void drainStage(EStage stage) {
    auto& ring = s_queryRings[static_cast<size_t>(stage)];
    for (int i = 0; i < QUERY_RING_SIZE; i++) {
        if (!ring.pending[i])
            continue;

        GLuint available = GL_FALSE;
        glGetQueryObjectuiv(ring.queries[i], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available)
            continue;

        GLuint64 elapsedNanoseconds = 0;
        s_glGetQueryObjectui64vEXT(ring.queries[i], GL_QUERY_RESULT, &elapsedNanoseconds);
        s_stageNanoseconds[ring.monitor[i]][static_cast<size_t>(stage)] += elapsedNanoseconds;
        ring.pending[i] = false;
    }
}

void drainAllStages() {
    if (!timerExtensionAvailable())
        return;
    for (size_t i = 0; i < STAGE_COUNT; i++)
        drainStage(static_cast<EStage>(i));
}

bool timersEnabledByConfig() {
    return g_pGlobalState && g_pGlobalState->config.debugTimers && **g_pGlobalState->config.debugTimers;
}

// A monitor with no recorded stage timings yet (no CScopedStageTimer bracket
// has closed for it since the last reset) has no entry in s_stageNanoseconds
// — treat that as all-zero rather than inserting one just to read it.
const std::array<uint64_t, STAGE_COUNT>& stageNanosecondsFor(MONITORID id) {
    static const std::array<uint64_t, STAGE_COUNT> ZERO{};
    const auto it = s_stageNanoseconds.find(id);
    return it != s_stageNanoseconds.end() ? it->second : ZERO;
}

std::string monitorLabel(MONITORID id) {
    for (const auto& monitor : State::monitorState()->monitors()) {
        if (monitor && monitor->m_id == id)
            return monitor->m_name;
    }
    return std::format("monitor {}", id);
}

std::string formatStats(eHyprCtlOutputFormat format) {
    drainAllStages();

    const bool timersOn    = timersEnabledByConfig();
    const bool timersReady = timerExtensionAvailable();

    if (format == eHyprCtlOutputFormat::FORMAT_JSON) {
        std::string json = "{\n  \"monitors\": [\n";
        bool        first = true;
        for (const auto& [id, counters] : s_counters) {
            if (!first)
                json += ",\n";
            first = false;
            json += std::format(
                "    {{\"name\": \"{}\", \"frames\": {}, \"windowGlassDraws\": {}, \"windowOpaqueSkipped\": {}, "
                "\"windowCacheHits\": {}, \"windowCacheMisses\": {}, \"windowDeferredResamples\": {}, \"windowPassDiscarded\": {}, "
                "\"layerGlassDraws\": {}, \"layerCacheHits\": {}, \"layerCacheMisses\": {}, \"blurPasses\": {}, "
                "\"sampledMegapixels\": {:.3f}, \"glassMegapixels\": {:.3f}, \"stageTimersAvgMicroseconds\": {{",
                escapeJSONStrings(monitorLabel(id)), counters.frames, counters.windowGlassDraws, counters.windowOpaqueSkipped,
                counters.windowCacheHits, counters.windowCacheMisses, counters.windowDeferredResamples, counters.windowPassDiscarded,
                counters.layerGlassDraws, counters.layerCacheHits, counters.layerCacheMisses, counters.blurPasses,
                counters.sampledMegapixels, counters.glassMegapixels);

            const auto& stageNanoseconds = stageNanosecondsFor(id);
            for (size_t i = 0; i < STAGE_COUNT; i++) {
                if (i > 0)
                    json += ", ";
                const double avgMicroseconds = (timersOn && timersReady && counters.frames > 0) ?
                    static_cast<double>(stageNanoseconds[i]) / static_cast<double>(counters.frames) / 1000.0 :
                    0.0;
                json += std::format("\"{}\": {:.3f}", STAGE_NAMES[i], avgMicroseconds);
            }
            json += "}}";
        }
        json += "\n  ],\n";
        json += std::format("  \"timersEnabled\": {}, \"timersAvailable\": {}\n}}\n", timersOn ? "true" : "false",
                             timersReady ? "true" : "false");
        return json;
    }

    std::string out = "hyprglass stats\n";

    if (!timersOn)
        out += "  stage timers: off (plugin:hyprglass:debug:timers = 0)\n";
    else if (!timersReady)
        out += "  stage timers: unavailable (GL_EXT_disjoint_timer_query not supported by this driver)\n";

    if (s_counters.empty())
        out += "  (no frames recorded yet)\n";

    out += std::format(
        "\n  {:<14} {:>8} {:>10} {:>12} {:>9} {:>9} {:>10} {:>9} {:>12} {:>10} {:>10} {:>10} {:>12} {:>11}\n", "monitor",
        "frames", "win_draws", "opaque_skip", "win_hit", "win_miss", "win_defer", "win_disc", "layer_draws", "layer_hit",
        "layer_miss", "blur_pass", "sampled_mpx", "glass_mpx");

    for (const auto& [id, counters] : s_counters) {
        out += std::format(
            "  {:<14} {:>8} {:>10} {:>12} {:>9} {:>9} {:>10} {:>9} {:>12} {:>10} {:>10} {:>10} {:>12.2f} {:>11.2f}\n",
            monitorLabel(id), counters.frames, counters.windowGlassDraws, counters.windowOpaqueSkipped, counters.windowCacheHits,
            counters.windowCacheMisses, counters.windowDeferredResamples, counters.windowPassDiscarded, counters.layerGlassDraws,
            counters.layerCacheHits, counters.layerCacheMisses, counters.blurPasses, counters.sampledMegapixels,
            counters.glassMegapixels);

        if (counters.frames > 0) {
            const double frames = static_cast<double>(counters.frames);
            out += std::format(
                "  {:<14} per frame: {:.2f} win draws, {:.2f} layer draws, {:.2f} blur passes, {:.3f} sampled mpx, {:.3f} glass mpx\n",
                "", static_cast<double>(counters.windowGlassDraws) / frames, static_cast<double>(counters.layerGlassDraws) / frames,
                static_cast<double>(counters.blurPasses) / frames, counters.sampledMegapixels / frames,
                counters.glassMegapixels / frames);

            if (timersOn && timersReady) {
                const auto& stageNanoseconds = stageNanosecondsFor(id);
                out += std::format("  {:<14} stage timers (avg microseconds/frame):", "");
                for (size_t i = 0; i < STAGE_COUNT; i++) {
                    const double avgMicroseconds = static_cast<double>(stageNanoseconds[i]) / frames / 1000.0;
                    out += std::format(" {}={:.2f}", STAGE_NAMES[i], avgMicroseconds);
                }
                out += "\n";
            }
        }
    }

    return out;
}

SP<SHyprCtlCommand> s_command;

} // namespace

void recordFrame(MONITORID monitor) {
    countersFor(monitor).frames++;
}

void recordWindowGlassDraw(MONITORID monitor) {
    countersFor(monitor).windowGlassDraws++;
}

void recordWindowOpaqueSkipped(MONITORID monitor) {
    countersFor(monitor).windowOpaqueSkipped++;
}

void recordWindowCacheHit(MONITORID monitor) {
    countersFor(monitor).windowCacheHits++;
}

void recordWindowCacheMiss(MONITORID monitor) {
    countersFor(monitor).windowCacheMisses++;
}

void recordWindowDeferredResample(MONITORID monitor) {
    countersFor(monitor).windowDeferredResamples++;
}

void recordWindowPassDiscarded(MONITORID monitor) {
    countersFor(monitor).windowPassDiscarded++;
}

void recordLayerGlassDraw(MONITORID monitor) {
    countersFor(monitor).layerGlassDraws++;
}

void recordLayerCacheHit(MONITORID monitor) {
    countersFor(monitor).layerCacheHits++;
}

void recordLayerCacheMiss(MONITORID monitor) {
    countersFor(monitor).layerCacheMisses++;
}

void recordBlurPasses(MONITORID monitor, uint64_t passes) {
    countersFor(monitor).blurPasses += passes;
}

void recordSampledPixels(MONITORID monitor, double pixels) {
    countersFor(monitor).sampledMegapixels += pixels / 1'000'000.0;
}

void recordGlassPixels(MONITORID monitor, double pixels) {
    countersFor(monitor).glassMegapixels += pixels / 1'000'000.0;
}

void resetCounters() {
    s_counters.clear();
    s_stageNanoseconds.clear();
}

void registerHyprCtlCommand(HANDLE handle) {
    s_command = HyprlandAPI::registerHyprCtlCommand(handle, SHyprCtlCommand{
        .name  = "hyprglass",
        .exact = false,
        .fn    = [](eHyprCtlOutputFormat format, std::string request) -> std::string {
            std::string_view rest{request};
            constexpr std::string_view PREFIX = "hyprglass";
            if (rest.starts_with(PREFIX))
                rest.remove_prefix(PREFIX.size());
            while (!rest.empty() && rest.front() == ' ')
                rest.remove_prefix(1);

            if (rest == "stats")
                return formatStats(format);

            if (rest == "stats reset") {
                resetCounters();
                return format == eHyprCtlOutputFormat::FORMAT_JSON ? "{\"ok\": true}\n" : "hyprglass: counters reset\n";
            }

            return "hyprglass: usage: hyprctl hyprglass stats [reset]  (prefix with j/ for JSON, e.g. hyprctl j/hyprglass stats)\n";
        },
    });
}

void unregisterHyprCtlCommand(HANDLE handle) {
    if (!s_command)
        return;
    HyprlandAPI::unregisterHyprCtlCommand(handle, s_command);
    s_command.reset();
}

void shutdown() {
    // No bracket should still be open here; end one defensively rather than deleting an active query.
    if (s_queryActive) {
        glEndQuery(GL_TIME_ELAPSED_EXT);
        s_queryActive = false;
    }

    for (auto& ring : s_queryRings) {
        for (auto& query : ring.queries) {
            if (query != 0)
                glDeleteQueries(1, &query);
        }
        ring.queries.fill(0);
        ring.pending.fill(false);
    }
}

CScopedStageTimer::CScopedStageTimer(EStage stage) : m_stage(stage) {
    if (!timersEnabledByConfig() || !timerExtensionAvailable() || s_queryActive)
        return;

    auto& ring = s_queryRings[static_cast<size_t>(stage)];
    drainStage(stage); // opportunistically free up any slots that just finished

    int slot = -1;
    for (int i = 0; i < QUERY_RING_SIZE; i++) {
        const int candidate = (ring.nextSlot + i) % QUERY_RING_SIZE;
        if (!ring.pending[candidate]) {
            slot = candidate;
            break;
        }
    }
    // Ring still full of unread results: skip this frame's sample for this
    // stage rather than stalling on the GPU to free one up.
    if (slot < 0)
        return;

    if (ring.queries[slot] == 0)
        glGenQueries(1, &ring.queries[slot]);

    // Captured now, not at drain time (see SStageQueryRing::monitor) — this
    // bracket always wraps GL work for whichever monitor is currently being
    // rendered.
    MONITORID monitorId = -1; // -1 mirrors Hyprland's own MONITOR_INVALID
    if (const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock())
        monitorId = monitor->m_id;
    ring.monitor[slot] = monitorId;

    glBeginQuery(GL_TIME_ELAPSED_EXT, ring.queries[slot]);
    ring.nextSlot = (slot + 1) % QUERY_RING_SIZE;

    m_slot        = slot;
    m_active      = true;
    s_queryActive = true;
}

CScopedStageTimer::~CScopedStageTimer() {
    if (!m_active)
        return;

    glEndQuery(GL_TIME_ELAPSED_EXT);
    s_queryRings[static_cast<size_t>(m_stage)].pending[m_slot] = true;
    s_queryActive                                              = false;
}

} // namespace Diagnostics
