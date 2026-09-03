#pragma once

#include "GlassLayerSurface.hpp"
#include "PluginConfig.hpp"
#include "ShaderManager.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>
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

    // X-ray: pre-window snapshot of each monitor's frame (wallpaper + bottom
    // layers, no windows), refreshed by CGlassSnapshotElement in the damaged
    // region. Keyed by MONITORID; entries go with the monitor.
    struct SBackgroundSnapshot {
        SP<Render::IFramebuffer> fb;
        // Monitor frames, counted at RENDER_BEGIN of a monitor render, and
        // the frame in which something last sampled the snapshot (a sampler's
        // draw() runs while that frame's pass is built). The snapshot is kept
        // fresh — every frame's damage copied — while anything has sampled it
        // within the last IDLE_FRAMES, whether or not a sampler is drawn in a
        // given frame: solitary fullscreen clients, exports and mirrors skip
        // the decorations, and treating such a frame as a lapse meant a
        // stale snapshot and a full redraw every time one went by.
        uint64_t frame          = 0;
        uint64_t requestedFrame = 0;
        uint64_t addedFrame     = 0; // the element is added once per frame
        // A whole-monitor damage frame has been copied since the snapshot was
        // (re)allocated; until then samplers use the live frame.
        bool complete = false;
    };
    std::unordered_map<MONITORID, SBackgroundSnapshot> backgroundSnapshots;

    // Per-namespace x-ray for layer surfaces (hg.layer xray / layers:namespace_xray)
    std::unordered_map<std::string, bool> layerNamespaceXray;

    // renderLayer hook
    CFunctionHook* renderLayerHook = nullptr;
    // damageSurface hook (live layer re-render)
    CFunctionHook* damageSurfaceHook = nullptr;
};

using Render::GL::g_pHyprOpenGL;

inline HANDLE                        PHANDLE = nullptr;
inline std::unique_ptr<SGlobalState> g_pGlobalState;

inline constexpr std::string_view PLUGIN_NAME        = "hyprglass";
inline constexpr std::string_view PLUGIN_DESCRIPTION = "Apple-style Liquid Glass effect";
inline constexpr std::string_view PLUGIN_AUTHOR      = "Hyprnux";
inline constexpr std::string_view PLUGIN_VERSION     = "1.0.0";
