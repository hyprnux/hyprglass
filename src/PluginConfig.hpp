#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <string_view>

inline constexpr std::string_view CONFIG_PREFIX = "plugin:hyprglass:";

// Theme window tags
inline constexpr std::string_view TAG_THEME_LIGHT = "hyprnux_theme_light";
inline constexpr std::string_view TAG_THEME_DARK  = "hyprnux_theme_dark";

// Sentinel: "not set by user, inherit from parent layer"
inline constexpr Hyprlang::FLOAT SENTINEL_FLOAT = -1.0;
inline constexpr Hyprlang::INT   SENTINEL_INT   = -1;

// Hardcoded per-theme defaults for settings that naturally differ between themes.
// Used as the final fallback when neither theme override nor global is set.
struct SThemeDefaults {
    float brightness;
    float contrast;
    float saturation;
    float vibrancy;
    float vibrancyDarkness;
    float adaptiveCorrection;
};

inline constexpr SThemeDefaults DARK_THEME_DEFAULTS  = {0.82f, 0.90f, 0.80f, 0.15f, 0.0f, 0.4f};
inline constexpr SThemeDefaults LIGHT_THEME_DEFAULTS = {1.12f, 0.92f, 0.85f, 0.12f, 0.0f, 0.4f};

namespace ConfigKeys {

// Global-only (no theme override)
inline constexpr auto ENABLED       = "plugin:hyprglass:enabled";
inline constexpr auto DEFAULT_THEME = "plugin:hyprglass:default_theme";

// Overridable — global level
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
inline constexpr auto BRIGHTNESS           = "plugin:hyprglass:brightness";
inline constexpr auto CONTRAST             = "plugin:hyprglass:contrast";
inline constexpr auto SATURATION           = "plugin:hyprglass:saturation";
inline constexpr auto VIBRANCY             = "plugin:hyprglass:vibrancy";
inline constexpr auto VIBRANCY_DARKNESS    = "plugin:hyprglass:vibrancy_darkness";
inline constexpr auto ADAPTIVE_CORRECTION  = "plugin:hyprglass:adaptive_correction";

// Overridable — dark theme overrides
inline constexpr auto DARK_BLUR_STRENGTH        = "plugin:hyprglass:dark:blur_strength";
inline constexpr auto DARK_BLUR_ITERATIONS      = "plugin:hyprglass:dark:blur_iterations";
inline constexpr auto DARK_REFRACTION_STRENGTH  = "plugin:hyprglass:dark:refraction_strength";
inline constexpr auto DARK_CHROMATIC_ABERRATION = "plugin:hyprglass:dark:chromatic_aberration";
inline constexpr auto DARK_FRESNEL_STRENGTH     = "plugin:hyprglass:dark:fresnel_strength";
inline constexpr auto DARK_SPECULAR_STRENGTH    = "plugin:hyprglass:dark:specular_strength";
inline constexpr auto DARK_GLASS_OPACITY        = "plugin:hyprglass:dark:glass_opacity";
inline constexpr auto DARK_EDGE_THICKNESS       = "plugin:hyprglass:dark:edge_thickness";
inline constexpr auto DARK_TINT_COLOR           = "plugin:hyprglass:dark:tint_color";
inline constexpr auto DARK_LENS_DISTORTION      = "plugin:hyprglass:dark:lens_distortion";
inline constexpr auto DARK_BRIGHTNESS           = "plugin:hyprglass:dark:brightness";
inline constexpr auto DARK_CONTRAST             = "plugin:hyprglass:dark:contrast";
inline constexpr auto DARK_SATURATION           = "plugin:hyprglass:dark:saturation";
inline constexpr auto DARK_VIBRANCY             = "plugin:hyprglass:dark:vibrancy";
inline constexpr auto DARK_VIBRANCY_DARKNESS    = "plugin:hyprglass:dark:vibrancy_darkness";
inline constexpr auto DARK_ADAPTIVE_CORRECTION  = "plugin:hyprglass:dark:adaptive_correction";

// Overridable — light theme overrides
inline constexpr auto LIGHT_BLUR_STRENGTH        = "plugin:hyprglass:light:blur_strength";
inline constexpr auto LIGHT_BLUR_ITERATIONS      = "plugin:hyprglass:light:blur_iterations";
inline constexpr auto LIGHT_REFRACTION_STRENGTH  = "plugin:hyprglass:light:refraction_strength";
inline constexpr auto LIGHT_CHROMATIC_ABERRATION = "plugin:hyprglass:light:chromatic_aberration";
inline constexpr auto LIGHT_FRESNEL_STRENGTH     = "plugin:hyprglass:light:fresnel_strength";
inline constexpr auto LIGHT_SPECULAR_STRENGTH    = "plugin:hyprglass:light:specular_strength";
inline constexpr auto LIGHT_GLASS_OPACITY        = "plugin:hyprglass:light:glass_opacity";
inline constexpr auto LIGHT_EDGE_THICKNESS       = "plugin:hyprglass:light:edge_thickness";
inline constexpr auto LIGHT_TINT_COLOR           = "plugin:hyprglass:light:tint_color";
inline constexpr auto LIGHT_LENS_DISTORTION      = "plugin:hyprglass:light:lens_distortion";
inline constexpr auto LIGHT_BRIGHTNESS           = "plugin:hyprglass:light:brightness";
inline constexpr auto LIGHT_CONTRAST             = "plugin:hyprglass:light:contrast";
inline constexpr auto LIGHT_SATURATION           = "plugin:hyprglass:light:saturation";
inline constexpr auto LIGHT_VIBRANCY             = "plugin:hyprglass:light:vibrancy";
inline constexpr auto LIGHT_VIBRANCY_DARKNESS    = "plugin:hyprglass:light:vibrancy_darkness";
inline constexpr auto LIGHT_ADAPTIVE_CORRECTION  = "plugin:hyprglass:light:adaptive_correction";

} // namespace ConfigKeys

// Pointers for a single config layer (global, dark override, or light override)
struct SOverridableConfig {
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
    Hyprlang::FLOAT* const* brightness          = nullptr;
    Hyprlang::FLOAT* const* contrast            = nullptr;
    Hyprlang::FLOAT* const* saturation          = nullptr;
    Hyprlang::FLOAT* const* vibrancy            = nullptr;
    Hyprlang::FLOAT* const* vibrancyDarkness    = nullptr;
    Hyprlang::FLOAT* const* adaptiveCorrection  = nullptr;
};

struct SPluginConfig {
    Hyprlang::INT* const* enabled      = nullptr;
    Hyprlang::INT* const* defaultTheme = nullptr;

    SOverridableConfig global;
    SOverridableConfig dark;
    SOverridableConfig light;
};

// Three-tier resolution: theme override > global > hardcoded fallback
[[nodiscard]] inline float resolveFloat(
    Hyprlang::FLOAT* const* themeOverride,
    Hyprlang::FLOAT* const* global,
    float hardcodedDefault = static_cast<float>(SENTINEL_FLOAT)
) {
    const float themeValue = static_cast<float>(**themeOverride);
    if (themeValue >= 0.0f) return themeValue;

    const float globalValue = static_cast<float>(**global);
    if (globalValue >= 0.0f) return globalValue;

    return hardcodedDefault;
}

[[nodiscard]] inline int64_t resolveInt(
    Hyprlang::INT* const* themeOverride,
    Hyprlang::INT* const* global,
    int64_t hardcodedDefault = SENTINEL_INT
) {
    const int64_t themeValue = **themeOverride;
    if (themeValue >= 0) return themeValue;

    const int64_t globalValue = **global;
    if (globalValue >= 0) return globalValue;

    return hardcodedDefault;
}

void registerConfig(HANDLE handle);
void initConfigPointers(HANDLE handle, SPluginConfig& config);
