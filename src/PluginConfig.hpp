#pragma once

#include <hyprland/src/config/shared/Types.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_map>

inline constexpr std::string_view CONFIG_PREFIX = "plugin:hyprglass:";

// Window tags for theme and preset selection
inline constexpr std::string_view TAG_THEME_PREFIX  = "hyprglass_theme_";
inline constexpr std::string_view TAG_PRESET_PREFIX = "hyprglass_preset_";

// Window tags for per-window enable/disable. Override the global `enabled` setting.
// `hyprglass_disabled` always wins if both are present.
inline constexpr std::string_view TAG_ENABLED  = "hyprglass_enabled";
inline constexpr std::string_view TAG_DISABLED = "hyprglass_disabled";

// Window tags for per-window x-ray (see `xray`): sample the glass from the
// pre-window snapshot of the frame (wallpaper and bottom layers only) instead
// of the live frame, so windows underneath never show through the glass. Both
// override the global setting; like Hyprland's `xray off` rule, `noxray` wins.
inline constexpr std::string_view TAG_XRAY   = "hyprglass_xray";
inline constexpr std::string_view TAG_NOXRAY = "hyprglass_noxray";

// Hyprland stores dynamic tags (`tagwindow` dispatcher, dynamic window rules)
// with a trailing '*'. CTagKeeper::isTagged() normalizes this for exact lookups,
// but code iterating getTags() or registering preset names must strip it itself
// so "firefox" and "firefox*" refer to the same preset.
inline std::string stripDynamicTagMarker(std::string_view tag) {
    if (tag.ends_with('*'))
        tag.remove_suffix(1);
    return std::string(tag);
}

// Sentinel: "not set by user, inherit from parent layer"
inline constexpr Hyprlang::FLOAT SENTINEL_FLOAT = -1.0;
inline constexpr Hyprlang::INT   SENTINEL_INT   = -1;

inline constexpr int MAX_PRESET_INHERITANCE_DEPTH = 8;

// Layer glass masking strategy: how hyprglass decides which pixels of a layer
// surface get the glass effect.
enum class ELayerMaskMode { AUTO, ALPHA, REGION };

// Parses "auto"/"alpha"/"region"; nullopt for anything else.
[[nodiscard]] std::optional<ELayerMaskMode> parseLayerMaskMode(std::string_view value);

namespace ConfigKeys {

// Global-only
inline constexpr auto ENABLED            = "plugin:hyprglass:enabled";
inline constexpr auto DEFAULT_THEME      = "plugin:hyprglass:default_theme";
inline constexpr auto DEFAULT_PRESET     = "plugin:hyprglass:default_preset";
inline constexpr auto MANAGE_WINDOW_BLUR = "plugin:hyprglass:manage_window_blur";
inline constexpr auto XRAY               = "plugin:hyprglass:xray";

// Preset keyword, registered as unscoped because Hyprlang does not dispatch
// scoped keyword handlers inside the plugin special category.
inline constexpr auto PRESET_KEYWORD = "preset";

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
inline constexpr auto ADAPTIVE_DIM          = "plugin:hyprglass:adaptive_dim";
inline constexpr auto ADAPTIVE_BOOST        = "plugin:hyprglass:adaptive_boost";

// Layer surface support
inline constexpr auto LAYERS_ENABLED            = "plugin:hyprglass:layers:enabled";
inline constexpr auto LAYERS_NAMESPACES         = "plugin:hyprglass:layers:namespaces";
inline constexpr auto LAYERS_EXCLUDE_NAMESPACES = "plugin:hyprglass:layers:exclude_namespaces";
inline constexpr auto LAYERS_PRESET             = "plugin:hyprglass:layers:preset";
inline constexpr auto LAYERS_NAMESPACE_PRESETS          = "plugin:hyprglass:layers:namespace_presets";
inline constexpr auto LAYERS_NAMESPACE_MASK_THRESHOLDS  = "plugin:hyprglass:layers:namespace_mask_thresholds";
inline constexpr auto LAYERS_NAMESPACE_LIVE_RESAMPLE    = "plugin:hyprglass:layers:namespace_live_resample";
inline constexpr auto LAYERS_LIVE_RESAMPLE              = "plugin:hyprglass:layers:live_resample";
inline constexpr auto LAYERS_LIVE_RESAMPLE_FPS          = "plugin:hyprglass:layers:live_resample_fps";
inline constexpr auto LAYERS_FORCE_LIVE_RESAMPLE        = "plugin:hyprglass:layers:force_live_resample";
inline constexpr auto LAYERS_MASK_MODE                  = "plugin:hyprglass:layers:mask_mode";
inline constexpr auto LAYERS_NAMESPACE_MASK_MODES       = "plugin:hyprglass:layers:namespace_mask_modes";
inline constexpr auto LAYERS_MANAGE_BLUR                = "plugin:hyprglass:layers:manage_blur";
inline constexpr auto LAYERS_NAMESPACE_XRAY             = "plugin:hyprglass:layers:namespace_xray";

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
inline constexpr auto DARK_ADAPTIVE_DIM          = "plugin:hyprglass:dark:adaptive_dim";
inline constexpr auto DARK_ADAPTIVE_BOOST        = "plugin:hyprglass:dark:adaptive_boost";

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
inline constexpr auto LIGHT_ADAPTIVE_DIM          = "plugin:hyprglass:light:adaptive_dim";
inline constexpr auto LIGHT_ADAPTIVE_BOOST        = "plugin:hyprglass:light:adaptive_boost";

} // namespace ConfigKeys

// Cached pointers for a single config layer (built-in dark/light/global)
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
    Hyprlang::FLOAT* const* adaptiveDim         = nullptr;
    Hyprlang::FLOAT* const* adaptiveBoost       = nullptr;
};

// Plain values for a user-defined preset layer (all sentinel = not set → inherit)
struct SPresetValues {
    float   blurStrength       = static_cast<float>(SENTINEL_FLOAT);
    int64_t blurIterations     = SENTINEL_INT;
    float   refractionStrength = static_cast<float>(SENTINEL_FLOAT);
    float   chromaticAberration = static_cast<float>(SENTINEL_FLOAT);
    float   fresnelStrength    = static_cast<float>(SENTINEL_FLOAT);
    float   specularStrength   = static_cast<float>(SENTINEL_FLOAT);
    float   glassOpacity       = static_cast<float>(SENTINEL_FLOAT);
    float   edgeThickness      = static_cast<float>(SENTINEL_FLOAT);
    int64_t tintColor          = SENTINEL_INT;
    float   lensDistortion     = static_cast<float>(SENTINEL_FLOAT);
    float   brightness         = static_cast<float>(SENTINEL_FLOAT);
    float   contrast           = static_cast<float>(SENTINEL_FLOAT);
    float   saturation         = static_cast<float>(SENTINEL_FLOAT);
    float   vibrancy           = static_cast<float>(SENTINEL_FLOAT);
    float   vibrancyDarkness   = static_cast<float>(SENTINEL_FLOAT);
    float   adaptiveDim        = static_cast<float>(SENTINEL_FLOAT);
    float   adaptiveBoost      = static_cast<float>(SENTINEL_FLOAT);
};

struct SCustomPreset {
    std::string   name;
    std::string   inherits;
    SPresetValues shared;
    SPresetValues dark;
    SPresetValues light;
};

struct StringConfigPtr {
    void* const*          dataptr = nullptr;
    const std::type_info* type    = nullptr;
};

inline std::string_view readStringConfig(const StringConfigPtr& ptr) {
    if (!ptr.dataptr || !ptr.type)
        return {};

    if (*ptr.type == typeid(Config::STRING)) {
        const auto* value = *reinterpret_cast<Config::STRING* const*>(ptr.dataptr);
        return value ? std::string_view(*value) : std::string_view{};
    }

    if (*ptr.type == typeid(Hyprlang::STRING)) {
        const auto value = *reinterpret_cast<Hyprlang::STRING const*>(ptr.dataptr);
        return value ? std::string_view(value) : std::string_view{};
    }

    return {};
}

struct SPluginConfig {
    Hyprlang::INT* const* enabled          = nullptr;
    // Glass replaces Hyprland's blur for glassed windows: when set, the plugin
    // marks them with the noblur window property so Hyprland composites them
    // against the live framebuffer (which contains the glass) instead of its
    // pre-frame cached blur.
    Hyprlang::INT* const* manageWindowBlur = nullptr;
    // X-ray, as in Hyprland's blur:xray: glass samples the pre-window snapshot
    // of the frame (wallpaper and bottom layers only), so windows underneath
    // never show through it. Per-window tags and per-layer settings override.
    Hyprlang::INT* const* xray             = nullptr;
    StringConfigPtr      defaultTheme;
    StringConfigPtr      defaultPreset;

    Hyprlang::INT* const* layersEnabled                  = nullptr;
    StringConfigPtr       layersNamespaces;
    StringConfigPtr       layersExcludeNamespaces;
    StringConfigPtr       layersPreset;
    StringConfigPtr       layersNamespacePresets;
    StringConfigPtr       layersNamespaceMaskThresholds;
    StringConfigPtr       layersNamespaceLiveResample;
    Hyprlang::INT* const* layersLiveResample             = nullptr;
    Hyprlang::INT* const* layersLiveResampleFps          = nullptr;
    Hyprlang::INT* const* layersForceLiveResample        = nullptr;
    StringConfigPtr       layersMaskMode;
    StringConfigPtr       layersNamespaceMaskModes;
    Hyprlang::INT* const* layersManageBlur               = nullptr;
    StringConfigPtr       layersNamespaceXray;

    SOverridableConfig global;
    SOverridableConfig dark;
    SOverridableConfig light;
};

// Context for preset-aware value resolution
struct SResolveContext {
    const std::string&                                    presetName;
    bool                                                  isDark;
    const SPluginConfig&                                  config;
    const std::unordered_map<std::string, SCustomPreset>& customPresets;
};

// Preset-aware resolution: preset chain → built-in theme → global → hardcoded
[[nodiscard]] float resolvePresetFloat(
    const SResolveContext& context,
    float SPresetValues::* presetField,
    Hyprlang::FLOAT* const* SOverridableConfig::* configField,
    float hardcodedDefault = static_cast<float>(SENTINEL_FLOAT));

[[nodiscard]] int64_t resolvePresetInt(
    const SResolveContext& context,
    int64_t SPresetValues::* presetField,
    Hyprlang::INT* const* SOverridableConfig::* configField,
    int64_t hardcodedDefault = SENTINEL_INT);

void registerConfig(HANDLE handle);
void initConfigPointers(HANDLE handle, SPluginConfig& config);

// Preset keyword handler (registered via addConfigKeyword)
Hyprlang::CParseResult handlePresetKeyword(const char* command, const char* value);

// Clear pending presets/layers before config re-parse (called from preConfigReload callback)
void clearPendingPresets();
void clearPendingLayers();

// Swap pending data into active maps (called from configReloaded callback)
void commitPendingPresets();
void commitPendingLayers();

// Validate config values and notify user of misconfigurations
void validateConfig();
