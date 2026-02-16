#pragma once

#include "PluginConfig.hpp"
#include "ShaderManager.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <memory>
#include <string_view>
#include <vector>

class CGlassDecoration;

struct SGlobalState {
    std::vector<WP<CGlassDecoration>> decorations;
    CShaderManager                    shaderManager;
    SPluginConfig                     config;

    // Shared blur temp framebuffer (reused across all decorations since they render sequentially)
    CFramebuffer blurTempFramebuffer;
};

inline HANDLE                        PHANDLE = nullptr;
inline std::unique_ptr<SGlobalState> g_pGlobalState;

inline constexpr std::string_view PLUGIN_NAME        = "hyprglass";
inline constexpr std::string_view PLUGIN_DESCRIPTION = "Apple-style Liquid Glass effect";
inline constexpr std::string_view PLUGIN_AUTHOR      = "Hyprnux";
inline constexpr std::string_view PLUGIN_VERSION     = "1.0.0";
