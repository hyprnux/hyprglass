#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/SharedDefs.hpp>

#include <cstdint>

// Per-monitor GPU work counters plus optional GL timer-query stage timings,
// exposed through `hyprctl hyprglass stats`. Every recording call below is a
// bare increment on a per-monitor struct — cheap enough to run unconditionally
// on the render hot path. Only the GL timer-query brackets are gated on
// plugin:hyprglass:debug:timers; the counters themselves are always on.
namespace Diagnostics {

// One monitor's counters since the last reset. Kept here (rather than in
// SGlobalState) because nothing outside this file reads or writes it —
// reporting happens entirely inside the hyprctl command handler.
struct SMonitorCounters {
    uint64_t frames                  = 0;
    uint64_t windowGlassDraws        = 0;
    uint64_t windowOpaqueSkipped     = 0;
    uint64_t windowCacheHits         = 0;
    uint64_t windowCacheMisses       = 0;
    uint64_t windowDeferredResamples = 0;
    uint64_t windowPassDiscarded     = 0;
    uint64_t layerGlassDraws     = 0;
    uint64_t layerCacheHits      = 0;
    uint64_t layerCacheMisses    = 0;
    uint64_t blurPasses          = 0;
    double   sampledMegapixels   = 0.0;
    double   glassMegapixels     = 0.0;
};

void recordFrame(MONITORID monitor);
void recordWindowGlassDraw(MONITORID monitor);
void recordWindowOpaqueSkipped(MONITORID monitor);
// Window background cache: a resample was skipped, ran because the
// background changed, was postponed pending more damage, or the element
// was dropped by simplify() before renderPass() ran.
void recordWindowCacheHit(MONITORID monitor);
void recordWindowCacheMiss(MONITORID monitor);
void recordWindowDeferredResample(MONITORID monitor);
void recordWindowPassDiscarded(MONITORID monitor);
void recordLayerGlassDraw(MONITORID monitor);
void recordLayerCacheHit(MONITORID monitor);
void recordLayerCacheMiss(MONITORID monitor);
void recordBlurPasses(MONITORID monitor, uint64_t passes);
void recordSampledPixels(MONITORID monitor, double pixels);
void recordGlassPixels(MONITORID monitor, double pixels);

// Clears every monitor's counters and the accumulated stage timings. Open GL
// queries are left to drain normally; they are GPU resources, not stats.
void resetCounters();

// Registers/unregisters "hyprctl hyprglass stats [reset]". Call once each
// from PLUGIN_INIT / PLUGIN_EXIT.
void registerHyprCtlCommand(HANDLE handle);
void unregisterHyprCtlCommand(HANDLE handle);

// Deletes every GL timer query object this file owns. Call once from
// PLUGIN_EXIT, after the render pass has finished tearing down (so no
// CScopedStageTimer bracket is still open) and before the GL context goes
// away.
void shutdown();

// The pipeline stage a CScopedStageTimer brackets. The layer stages wrap
// their whole pre-/post-surface function (sample+blur+redirect, mask+
// composite) rather than the individual GlassRenderer calls: GL forbids two
// concurrent GL_TIME_ELAPSED queries, so when a layer bracket is open, the
// SampleBackground/BlurBackground/ApplyGlassEffect brackets its call chain
// opens underneath it simply no-op instead of nesting. Only the window path
// (which has no outer bracket) ever measures those three directly.
enum class EStage : int {
    SampleBackground = 0,
    BlurBackground,
    ApplyGlassEffect,
    LayerSample,
    LayerComposite,
    Count,
};

// RAII bracket for one call to a pipeline stage. No-ops (cheaply: one global
// bool read) when plugin:hyprglass:debug:timers is off, when
// GL_EXT_disjoint_timer_query is unavailable, or when another query is
// already open — never blocks, never nests.
class CScopedStageTimer {
  public:
    explicit CScopedStageTimer(EStage stage);
    ~CScopedStageTimer();

    CScopedStageTimer(const CScopedStageTimer&)            = delete;
    CScopedStageTimer& operator=(const CScopedStageTimer&) = delete;

  private:
    EStage m_stage;
    int    m_slot   = -1;
    bool   m_active = false;
};

} // namespace Diagnostics
