#pragma once

#include "GlassLayerSurface.hpp"
#include "PluginConfig.hpp"
#include "ShaderManager.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/SharedDefs.hpp>
#include <hyprutils/signal/Listener.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

class CGlassDecoration;

struct SGlobalState {
    // Event listeners are owned here so PLUGIN_EXIT unregisters them. Static
    // listeners outlived the plugin and fired after unload -> SEGV on reload.
    std::vector<Hyprutils::Signal::CHyprSignalListener> listeners;

    // These WPs wrap the UP<IHyprWindowDecoration> Hyprland owns: dereference with
    // .get() only, lock() asserts on a unique-owned pointer and terminates.
    std::vector<WP<CGlassDecoration>> decorations;
    CShaderManager                    shaderManager;
    SPluginConfig                     config;

    // User-defined presets (populated from config keyword, swapped in on configReloaded)
    std::unordered_map<std::string, SCustomPreset> customPresets;

    // Shared blur temp framebuffer (reused across all decorations since they render sequentially)
    SP<Render::IFramebuffer> blurTempFramebuffer;

    // Layer surface glass state (one per tracked layer, keyed by raw pointer).
    // shared_ptr so CGlassLayerPassElement can hold a copy that survives map erasure mid-frame.
    std::unordered_map<Desktop::View::CLayerSurface*, std::shared_ptr<CGlassLayerSurface>> layerSurfaces;

    // Parsed namespace whitelist (empty = match all when layers enabled)
    std::unordered_set<std::string> layerNamespaceFilter;
    // Parsed namespace blacklist (always excluded, takes priority over whitelist)
    std::unordered_set<std::string> layerNamespaceExclude;
    // Per-namespace preset overrides (namespace → preset name)
    std::unordered_map<std::string, std::string> layerNamespacePresets;
    // Per-namespace mask alpha threshold (namespace → threshold, default 0.001)
    std::unordered_map<std::string, float> layerNamespaceMaskThresholds;
    // Per-namespace live resample override (namespace → enabled)
    std::unordered_map<std::string, bool> layerNamespaceLiveResample;
    // Per-namespace mask mode override (namespace → mode)
    std::unordered_map<std::string, ELayerMaskMode> layerNamespaceMaskModes;

    // Per-monitor generation counter, incremented when the scene behind layers
    // changes on that monitor. Layer surfaces compare to their cached value to
    // skip redundant blur work. Per-monitor avoids cross-monitor feedback loops
    // where re-sampling on an idle monitor captures its own stale glass output.
    // Keyed by MONITORID rather than CMonitor* so the code builds on both
    // Hyprland <= 0.55.x (global CMonitor) and git (Monitor::CMonitor), and a
    // stale entry can never alias a reallocated monitor object.
    std::unordered_map<MONITORID, uint64_t> sceneGeneration;

    uint64_t getSceneGeneration(const PHLMONITOR& mon) const {
        if (!mon)
            return 0;
        auto it = sceneGeneration.find(mon->m_id);
        return it != sceneGeneration.end() ? it->second : 0;
    }
    void bumpSceneGeneration(const PHLMONITOR& mon) {
        if (mon)
            sceneGeneration[mon->m_id]++;
    }

    // Render-order fingerprint: a per-monitor running hash folded from every
    // glass-eligible window's identity/geometry/alpha in z-order, compared frame
    // to frame to catch stacking/membership changes (e.g. a window closing behind
    // a cached glass window) that no other event bumps scene generation for.
    struct SRenderFingerprint {
        size_t runningHash = 0;
        size_t lastHash    = 0;
    };
    std::unordered_map<MONITORID, SRenderFingerprint> renderFingerprints;

    // Surface-commit observation driving the layer live resample: the
    // subscriptions that discover surfaces, and one commit/destroy pair per
    // watched surface. Owned here for the same reason as `listeners`: PLUGIN_EXIT
    // must drop them before render teardown.
    struct SWatchedSurface {
        Hyprutils::Signal::CHyprSignalListener commit;
        Hyprutils::Signal::CHyprSignalListener destroy;
    };
    std::vector<Hyprutils::Signal::CHyprSignalListener>         observerListeners;
    std::unordered_map<WP<CWLSurfaceResource>, SWatchedSurface> watchedSurfaces;

    // Mirrors anySelfSampleConfigured(), refreshed with the observer. Read on every
    // watched commit, so it must stay a plain bool and not a config walk.
    bool selfSampleConfigured = false;

    // Bumped on RENDER_BEGIN, so one value per monitor frame. 0 is reserved for
    // renders we do not manage (snapshots, screencopy, overview framebuffers).
    uint64_t frameSerial = 0;

    // Hyprland renders a floating window that is allowed over fullscreen more
    // than once per frame. The redundant copy is redirected into this pass,
    // which is dropped instead of rendered.
    // sink before guard: members destruct in reverse declaration order, and the
    // guard must point m_currentPass away from sink before sink dies.
    struct SDedupeState {
        Render::CRenderPass                  sink;
        UP<Hyprutils::Utils::CScopeGuard>    guard;
        std::vector<Desktop::View::CWindow*> dropped; // identity only, never dereferenced
        bool                                 sawFullscreen = false;

        // one workspace pass
        void resetEpoch() {
            guard.reset();
            sink.clear();
            sawFullscreen = false;
        }

        // one monitor frame: `dropped` outlives the epoch, a window is rendered
        // by the pass of another visible workspace too and may only lose one copy
        void reset() {
            resetEpoch();
            dropped.clear();
        }
    } dedupe;

    // renderLayer hook
    CFunctionHook* renderLayerHook = nullptr;
};

using Render::GL::g_pHyprOpenGL;

inline HANDLE                        PHANDLE = nullptr;
inline std::unique_ptr<SGlobalState> g_pGlobalState;

// Decoration registered for this window, or nullptr. Borrowed, never owned.
CGlassDecoration* glassDecorationFor(const PHLWINDOW& window);

inline constexpr std::string_view PLUGIN_NAME        = "hyprglass";
inline constexpr std::string_view PLUGIN_DESCRIPTION = "Apple-style Liquid Glass effect";
inline constexpr std::string_view PLUGIN_AUTHOR      = "Hyprnux";
inline constexpr std::string_view PLUGIN_VERSION     = "1.0.0";
