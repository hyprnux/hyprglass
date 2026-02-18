#pragma once

#include "PluginConfig.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

// ── All default values in one place ──────────────────────────────────────────

// Per-theme hardcoded fallbacks for settings that naturally differ between
// themes. Used as the final tier in the resolution chain.
struct SThemeDefaults {
    float brightness;
    float contrast;
    float saturation;
    float vibrancy;
    float vibrancyDarkness;
    float adaptiveDim;
    float adaptiveBoost;
};

inline constexpr SThemeDefaults DARK_THEME_DEFAULTS  = {0.82f, 0.90f, 0.80f, 0.15f, 0.0f, 0.4f, 0.0f};
inline constexpr SThemeDefaults LIGHT_THEME_DEFAULTS = {1.12f, 0.92f, 0.85f, 0.12f, 0.0f, 0.0f, 0.4f};

// Global config defaults — values registered with Hyprlang for effect settings.
// Theme-sensitive settings (brightness, contrast, etc.) use SENTINEL at global
// level so they fall through to the per-theme hardcoded defaults above.
namespace GlobalDefaults {
    inline constexpr float   BLUR_STRENGTH        = 2.0f;
    inline constexpr int64_t BLUR_ITERATIONS      = 3;
    inline constexpr float   REFRACTION_STRENGTH  = 0.6f;
    inline constexpr float   CHROMATIC_ABERRATION = 0.5f;
    inline constexpr float   FRESNEL_STRENGTH     = 0.6f;
    inline constexpr float   SPECULAR_STRENGTH    = 0.8f;
    inline constexpr float   GLASS_OPACITY        = 1.0f;
    inline constexpr float   EDGE_THICKNESS       = 0.06f;
    inline constexpr int64_t TINT_COLOR           = 0x8899aa22;
    inline constexpr float   LENS_DISTORTION      = 0.5f;
} // namespace GlobalDefaults

// ── Built-in presets ─────────────────────────────────────────────────────────
// To add a new built-in preset: define a make*() function and register it
// in getAll().

namespace BuiltInPresets {

inline SCustomPreset makeHighContrast() {
    SCustomPreset p;
    p.name = "high_contrast";

    p.shared.blurStrength        = 1.2f;
    p.shared.blurIterations      = 2;
    p.shared.lensDistortion      = 0.5f;
    p.shared.refractionStrength  = 1.2f;
    p.shared.chromaticAberration = 0.25f;
    p.shared.fresnelStrength     = 0.3f;
    p.shared.specularStrength    = 0.8f;
    p.shared.glassOpacity        = 1.0f;
    p.shared.edgeThickness       = 0.06f;

    p.dark.brightness         = 0.82f;
    p.dark.contrast           = 1.14f;
    p.dark.saturation         = 0.92f;
    p.dark.vibrancy           = 0.5f;
    p.dark.vibrancyDarkness   = 0.2f;
    p.dark.adaptiveDim        = 0.25f;
    p.dark.tintColor          = 0x02142aa9;

    p.light.brightness         = 1.0f;
    p.light.contrast           = 0.92f;
    p.light.saturation         = 0.8f;
    p.light.vibrancy           = 0.12f;
    p.light.vibrancyDarkness   = 5.0f;
    p.light.adaptiveBoost      = 0.15f;
    p.light.tintColor          = 0xc2cddb33;

    return p;
}

inline SCustomPreset makeSubtle() {
    SCustomPreset p;
    p.name = "subtle";

    p.shared.blurStrength        = 1.0f;
    p.shared.refractionStrength  = 0.3f;
    p.shared.chromaticAberration = 0.2f;
    p.shared.fresnelStrength     = 0.3f;
    p.shared.specularStrength    = 0.4f;

    return p;
}

inline SCustomPreset makeClear() {
    SCustomPreset p;
    p.name = "clear";

    p.shared.blurStrength        = 0.0f;
    p.shared.refractionStrength  = 0.3f;
    p.shared.chromaticAberration = 0.2f;
    p.shared.fresnelStrength     = 0.3f;
    p.shared.specularStrength    = 0.4f;

    return p;
}

inline SCustomPreset makeGlass() {
    SCustomPreset p;
    p.name = "glass";

    p.shared.blurStrength        = 1.0f;
    p.shared.blurIterations      = 2;
    p.shared.lensDistortion      = 0.3f;
    p.shared.refractionStrength  = 8.0f;
    p.shared.chromaticAberration = 0.5f;
    p.shared.fresnelStrength     = 0.4f;
    p.shared.specularStrength    = 0.8f;
    p.shared.glassOpacity        = 1.0f;
    p.shared.edgeThickness       = 0.06f;
    p.shared.tintColor           = 0xffffff00;

    p.dark.adaptiveDim           = 0.3f;
    p.light.adaptiveBoost        = 0.3f;

    return p;
}

inline std::unordered_map<std::string, SCustomPreset> getAll() {
    std::unordered_map<std::string, SCustomPreset> presets;

    auto add = [&](SCustomPreset p) { presets[p.name] = std::move(p); };

    add(makeHighContrast());
    add(makeSubtle());
    add(makeClear());
    add(makeGlass());

    return presets;
}

} // namespace BuiltInPresets
