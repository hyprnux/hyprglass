#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <memory>
#include <vector>

class CLiquidGlassDecoration;

struct SGlobalState {
    std::vector<WP<CLiquidGlassDecoration>> decorations;
    SShader                                  shader;
    bool                                     shaderInitialized = false;

    // Shader uniform locations
    GLint locRefractionStrength    = -1;
    GLint locChromaticAberration   = -1;
    GLint locFresnelStrength       = -1;
    GLint locSpecularStrength      = -1;
    GLint locGlassOpacity          = -1;
    GLint locEdgeThickness         = -1;
    GLint locUvPadding             = -1;
    GLint locTintColor             = -1;
    GLint locTintAlpha             = -1;
    GLint locLensDistortion        = -1;
    GLint locBackgroundBrightness  = -1;
    GLint locBackgroundSaturation  = -1;
    GLint locTexRaw                = -1;
    GLint locRoundingPower         = -1;

    // Blur shader
    SShader blurShader;
    bool    blurShaderInitialized  = false;
    GLint   locBlurDirection       = -1;
    GLint   locBlurRadius          = -1;
};

inline HANDLE                        PHANDLE = nullptr;
inline std::unique_ptr<SGlobalState> g_pGlobalState;

inline const char* PLUGIN_NAME        = "liquid-glass";
inline const char* PLUGIN_DESCRIPTION = "Apple-style Liquid Glass effect";
inline const char* PLUGIN_AUTHOR      = "Hyprnux";
inline const char* PLUGIN_VERSION     = "1.0.0";

void initShaderIfNeeded();
