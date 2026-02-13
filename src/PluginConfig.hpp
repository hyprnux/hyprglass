#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string_view>

inline constexpr std::string_view CONFIG_PREFIX = "plugin:hyprglass:";

// Theme window tags
inline constexpr std::string_view TAG_THEME_LIGHT = "hyprnux_theme_light";
inline constexpr std::string_view TAG_THEME_DARK  = "hyprnux_theme_dark";

namespace ConfigKeys {

inline constexpr auto ENABLED              = "plugin:hyprglass:enabled";
inline constexpr auto BLUR_STRENGTH        = "plugin:hyprglass:blur_strength";
inline constexpr auto BLUR_ITERATIONS      = "plugin:hyprglass:blur_iterations";
inline constexpr auto REFRACTION_STRENGTH  = "plugin:hyprglass:refraction_strength";
inline constexpr auto CHROMATIC_ABERRATION = "plugin:hyprglass:chromatic_aberration";
inline constexpr auto FRESNEL_STRENGTH     = "plugin:hyprglass:fresnel_strength";
inline constexpr auto SPECULAR_STRENGTH    = "plugin:hyprglass:specular_strength";
inline constexpr auto GLASS_OPACITY        = "plugin:hyprglass:glass_opacity";
inline constexpr auto EDGE_THICKNESS       = "plugin:hyprglass:edge_thickness";
inline constexpr auto TINT_COLOR           = "plugin:hyprglass:tint_color";
inline constexpr auto LENS_DISTORTION      = "plugin:hyprglass:lens_distortion";
inline constexpr auto DEFAULT_THEME        = "plugin:hyprglass:default_theme";

inline constexpr auto DARK_BRIGHTNESS        = "plugin:hyprglass:dark:brightness";
inline constexpr auto DARK_CONTRAST          = "plugin:hyprglass:dark:contrast";
inline constexpr auto DARK_SATURATION        = "plugin:hyprglass:dark:saturation";
inline constexpr auto DARK_VIBRANCY          = "plugin:hyprglass:dark:vibrancy";
inline constexpr auto DARK_VIBRANCY_DARKNESS = "plugin:hyprglass:dark:vibrancy_darkness";
inline constexpr auto DARK_ADAPTIVE_DIM      = "plugin:hyprglass:dark:adaptive_dim";

inline constexpr auto LIGHT_BRIGHTNESS        = "plugin:hyprglass:light:brightness";
inline constexpr auto LIGHT_CONTRAST          = "plugin:hyprglass:light:contrast";
inline constexpr auto LIGHT_SATURATION        = "plugin:hyprglass:light:saturation";
inline constexpr auto LIGHT_VIBRANCY          = "plugin:hyprglass:light:vibrancy";
inline constexpr auto LIGHT_VIBRANCY_DARKNESS = "plugin:hyprglass:light:vibrancy_darkness";
inline constexpr auto LIGHT_ADAPTIVE_BOOST    = "plugin:hyprglass:light:adaptive_boost";

} // namespace ConfigKeys

struct SPluginConfig {
    Hyprlang::INT* const*   enabled             = nullptr;
    Hyprlang::FLOAT* const* blurStrength        = nullptr;
    Hyprlang::INT* const*   blurIterations      = nullptr;
    Hyprlang::FLOAT* const* refractionStrength  = nullptr;
    Hyprlang::FLOAT* const* chromaticAberration = nullptr;
    Hyprlang::FLOAT* const* fresnelStrength     = nullptr;
    Hyprlang::FLOAT* const* specularStrength    = nullptr;
    Hyprlang::FLOAT* const* glassOpacity        = nullptr;
    Hyprlang::FLOAT* const* edgeThickness       = nullptr;
    Hyprlang::INT* const*   tintColor           = nullptr;
    Hyprlang::FLOAT* const* lensDistortion      = nullptr;
    Hyprlang::INT* const*   defaultTheme        = nullptr;

    Hyprlang::FLOAT* const* darkBrightness       = nullptr;
    Hyprlang::FLOAT* const* darkContrast         = nullptr;
    Hyprlang::FLOAT* const* darkSaturation       = nullptr;
    Hyprlang::FLOAT* const* darkVibrancy         = nullptr;
    Hyprlang::FLOAT* const* darkVibrancyDarkness = nullptr;
    Hyprlang::FLOAT* const* darkAdaptiveDim      = nullptr;

    Hyprlang::FLOAT* const* lightBrightness       = nullptr;
    Hyprlang::FLOAT* const* lightContrast         = nullptr;
    Hyprlang::FLOAT* const* lightSaturation       = nullptr;
    Hyprlang::FLOAT* const* lightVibrancy         = nullptr;
    Hyprlang::FLOAT* const* lightVibrancyDarkness = nullptr;
    Hyprlang::FLOAT* const* lightAdaptiveBoost    = nullptr;
};

void registerConfig(HANDLE handle);
void initConfigPointers(HANDLE handle, SPluginConfig& config);
