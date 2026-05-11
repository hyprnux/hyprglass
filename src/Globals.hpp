#pragma once

#include "GlassLayerSurface.hpp"
#include "PluginConfig.hpp"
#include "ShaderManager.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

class CGlassDecoration;

struct SGlobalState {
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

    // Per-monitor generation counter, incremented when the scene behind layers
    // changes on that monitor. Layer surfaces compare to their cached value to
    // skip redundant blur work. Per-monitor avoids cross-monitor feedback loops
    // where re-sampling on an idle monitor captures its own stale glass output.
    std::unordered_map<CMonitor*, uint64_t> sceneGeneration;

    uint64_t getSceneGeneration(CMonitor* mon) const {
        auto it = sceneGeneration.find(mon);
        return it != sceneGeneration.end() ? it->second : 0;
    }
    void bumpSceneGeneration(CMonitor* mon) { sceneGeneration[mon]++; }

    // renderLayer hook
    CFunctionHook* renderLayerHook = nullptr;
};

inline HANDLE                        PHANDLE = nullptr;
inline std::unique_ptr<SGlobalState> g_pGlobalState;

inline constexpr std::string_view PLUGIN_NAME        = "hyprglass";
inline constexpr std::string_view PLUGIN_DESCRIPTION = "Apple-style Liquid Glass effect";
inline constexpr std::string_view PLUGIN_AUTHOR      = "Hyprnux";
inline constexpr std::string_view PLUGIN_VERSION     = "1.0.0";
