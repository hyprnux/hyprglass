#pragma once

#include <hyprland/src/config/shared/Types.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <typeinfo>
#include <unordered_map>

// Window tags for theme and preset selection
inline constexpr std::string_view TAG_THEME_PREFIX  = "hyprglass_theme_";
inline constexpr std::string_view TAG_PRESET_PREFIX = "hyprglass_preset_";

// Window tags for per-window enable/disable. Override the global `enabled` setting.
// `hyprglass_disabled` always wins if both are present.
inline constexpr std::string_view TAG_ENABLED  = "hyprglass_enabled";
inline constexpr std::string_view TAG_DISABLED = "hyprglass_disabled";

// Hyprland stores dynamic tags (the `tag` window rule effect) with a trailing '*'.
// CTagKeeper::isTagged() normalizes this for exact lookups, but code iterating
// getTags() or registering preset names must strip it itself so "firefox" and
// "firefox*" refer to the same preset.
inline std::string stripDynamicTagMarker(std::string_view tag) {
    if (tag.ends_with('*'))
        tag.remove_suffix(1);
    return std::string(tag);
}

// Sentinel: "not set by user, inherit from parent layer"
inline constexpr Config::FLOAT   SENTINEL_FLOAT = -1.0;
inline constexpr Config::INTEGER SENTINEL_INT   = -1;

inline constexpr int MAX_PRESET_INHERITANCE_DEPTH = 8;

// hg.layer() namespace that whitelists every layer; exact match, no globbing.
inline const std::string LAYER_NAMESPACE_WILDCARD = "*";

// Layer glass masking strategy: how hyprglass decides which pixels of a layer
// surface get the glass effect.
enum class ELayerMaskMode { AUTO, ALPHA, REGION, CONTOUR };

// Parses "auto"/"alpha"/"region"/"contour"; nullopt for anything else.
[[nodiscard]] std::optional<ELayerMaskMode> parseLayerMaskMode(std::string_view value);

// Fixed glass shape of a layer namespace: the layer box inset per side and
// rounded by one corner radius, both in logical px. A negative inset reaches
// past that layer edge, a negative radius makes a capsule.
struct SLayerShape {
    float insetTop    = 0.0f;
    float insetRight  = 0.0f;
    float insetBottom = 0.0f;
    float insetLeft   = 0.0f;
    float radius      = 0.0f;
};

// Parses "inset:radius" or "top:right:bottom:left:radius"; nullopt for anything else.
[[nodiscard]] std::optional<SLayerShape> parseLayerShape(std::string_view value);

// Instrumentation mode (plugin:hyprglass:debug:mode). Isolates the render-pass
// cost from the GL pipeline cost for A/B measurement: hints_only skips all GL
// work in the pass elements while keeping their boundingBox/needsLiveBlur/
// disableSimplification hints; gl_work_only runs the GL pipeline as normal but
// forces needsLiveBlur/disableSimplification off on both pass elements.
enum class EDebugMode { OFF, HINTS_ONLY, GL_WORK_ONLY };

// Parses "off"/"hints_only"/"gl_work_only"; nullopt for anything else.
[[nodiscard]] std::optional<EDebugMode> parseDebugMode(std::string_view value);

// Reads plugin:hyprglass:debug:mode, falling back to OFF for an unset global
// state or an unrecognized value (validateConfig() warns about the latter).
[[nodiscard]] EDebugMode currentDebugMode();

namespace ConfigKeys {

// Global-only
inline constexpr auto ENABLED             = "plugin:hyprglass:enabled";
inline constexpr auto DEFAULT_THEME       = "plugin:hyprglass:default_theme";
inline constexpr auto DEFAULT_PRESET      = "plugin:hyprglass:default_preset";
inline constexpr auto MANAGE_WINDOW_BLUR  = "plugin:hyprglass:manage_window_blur";
inline constexpr auto SKIP_OPAQUE_WINDOWS = "plugin:hyprglass:skip_opaque_windows";
inline constexpr auto BLUR_FOLD           = "plugin:hyprglass:blur_fold";

// Performance diagnostics
inline constexpr auto DEBUG_MODE   = "plugin:hyprglass:debug:mode";
inline constexpr auto DEBUG_TIMERS = "plugin:hyprglass:debug:timers";

// Preset keyword, registered as unscoped because Hyprlang does not dispatch
// scoped keyword handlers inside the plugin special category.
inline constexpr auto PRESET_KEYWORD = "preset";

// Overridable — global level
inline constexpr auto BLUR_STRENGTH        = "plugin:hyprglass:blur_strength";
inline constexpr auto BLUR_ITERATIONS      = "plugin:hyprglass:blur_iterations";
inline constexpr auto NOISE_STRENGTH       = "plugin:hyprglass:noise_strength";
inline constexpr auto REFRACTION_STRENGTH  = "plugin:hyprglass:refraction_strength";
inline constexpr auto CHROMATIC_ABERRATION = "plugin:hyprglass:chromatic_aberration";
inline constexpr auto FRESNEL_STRENGTH     = "plugin:hyprglass:fresnel_strength";
inline constexpr auto SPECULAR_STRENGTH    = "plugin:hyprglass:specular_strength";
inline constexpr auto SPECULAR_ANGLE       = "plugin:hyprglass:specular_angle";
inline constexpr auto GLASS_OPACITY        = "plugin:hyprglass:glass_opacity";
inline constexpr auto EDGE_THICKNESS       = "plugin:hyprglass:edge_thickness";
inline constexpr auto EDGE_THICKNESS_PX    = "plugin:hyprglass:edge_thickness_px";
inline constexpr auto EDGE_HEIGHT          = "plugin:hyprglass:edge_height";
inline constexpr auto FRESNEL_REFLECTION   = "plugin:hyprglass:fresnel_reflection";
inline constexpr auto TINT_COLOR           = "plugin:hyprglass:tint_color";
inline constexpr auto LENS_DISTORTION      = "plugin:hyprglass:lens_distortion";
inline constexpr auto BRIGHTNESS           = "plugin:hyprglass:brightness";
inline constexpr auto CONTRAST             = "plugin:hyprglass:contrast";
inline constexpr auto SATURATION           = "plugin:hyprglass:saturation";
inline constexpr auto VIBRANCY             = "plugin:hyprglass:vibrancy";
inline constexpr auto VIBRANCY_DARKNESS    = "plugin:hyprglass:vibrancy_darkness";
inline constexpr auto ADAPTIVE_DIM          = "plugin:hyprglass:adaptive_dim";
inline constexpr auto ADAPTIVE_BOOST        = "plugin:hyprglass:adaptive_boost";
inline constexpr auto REFRACTION_FLOW       = "plugin:hyprglass:refraction_flow";
inline constexpr auto REFRACTION_SPREAD     = "plugin:hyprglass:refraction_spread";
inline constexpr auto FRESNEL_TINT          = "plugin:hyprglass:fresnel_tint";
inline constexpr auto BEVEL_STRENGTH        = "plugin:hyprglass:bevel_strength";
inline constexpr auto BEVEL_SIZE            = "plugin:hyprglass:bevel_size";
inline constexpr auto FRESNEL_COLOR         = "plugin:hyprglass:fresnel_color";
inline constexpr auto BEVEL_COLOR           = "plugin:hyprglass:bevel_color";
inline constexpr auto BEVEL_TINT            = "plugin:hyprglass:bevel_tint";
inline constexpr auto BEVEL_ANGLE           = "plugin:hyprglass:bevel_angle";
inline constexpr auto BEVEL_SHADOW          = "plugin:hyprglass:bevel_shadow";
inline constexpr auto SELF_SAMPLE           = "plugin:hyprglass:self_sample";
inline constexpr auto XRAY                  = "plugin:hyprglass:xray";
inline constexpr auto FOCUS_LIGHT           = "plugin:hyprglass:focus_light";
inline constexpr auto FOCUS_LIGHT_COLOR     = "plugin:hyprglass:focus_light_color";
inline constexpr auto FROST                 = "plugin:hyprglass:frost";
inline constexpr auto FROST_BLUR            = "plugin:hyprglass:frost_blur";

// Layer surface support
inline constexpr auto LAYERS_ENABLED            = "plugin:hyprglass:layers:enabled";
inline constexpr auto LAYERS_NAMESPACES         = "plugin:hyprglass:layers:namespaces";
inline constexpr auto LAYERS_EXCLUDE_NAMESPACES = "plugin:hyprglass:layers:exclude_namespaces";
inline constexpr auto LAYERS_PRESET             = "plugin:hyprglass:layers:preset";
inline constexpr auto LAYERS_NAMESPACE_PRESETS          = "plugin:hyprglass:layers:namespace_presets";
inline constexpr auto LAYERS_MASK_THRESHOLD             = "plugin:hyprglass:layers:mask_threshold";
inline constexpr auto LAYERS_NAMESPACE_MASK_THRESHOLDS  = "plugin:hyprglass:layers:namespace_mask_thresholds";
inline constexpr auto LAYERS_NAMESPACE_LIVE_RESAMPLE    = "plugin:hyprglass:layers:namespace_live_resample";
inline constexpr auto LAYERS_LIVE_RESAMPLE              = "plugin:hyprglass:layers:live_resample";
inline constexpr auto LAYERS_LIVE_RESAMPLE_FPS          = "plugin:hyprglass:layers:live_resample_fps";
inline constexpr auto LAYERS_FORCE_LIVE_RESAMPLE        = "plugin:hyprglass:layers:force_live_resample";
inline constexpr auto LAYERS_MASK_MODE                  = "plugin:hyprglass:layers:mask_mode";
inline constexpr auto LAYERS_NAMESPACE_MASK_MODES       = "plugin:hyprglass:layers:namespace_mask_modes";
// Alpha mask only: alpha range above mask_threshold over which layer glass fades in; 0 keeps the hard mask.
inline constexpr auto LAYERS_MASK_FEATHER               = "plugin:hyprglass:layers:mask_feather";
inline constexpr auto LAYERS_NAMESPACE_MASK_FEATHERS    = "plugin:hyprglass:layers:namespace_mask_feathers";
inline constexpr auto LAYERS_NAMESPACE_SHAPES           = "plugin:hyprglass:layers:namespace_shapes";
inline constexpr auto LAYERS_MANAGE_BLUR                = "plugin:hyprglass:layers:manage_blur";

// Subsurface item glass support (see GlassSubsurfaceState).
// Preset resolution: subsurfaces:preset -> layers:preset (if set) -> window default preset.
inline constexpr auto SUBSURFACES_ENABLED = "plugin:hyprglass:subsurfaces:enabled";
inline constexpr auto SUBSURFACES_PRESET  = "plugin:hyprglass:subsurfaces:preset";
// Glass shape radius, logical px. -1 (default) = capsule: min(w,h)/2 of the
// glass box (the item's blur-region extents, not necessarily its whole
// surface box). >= 0 = that exact radius, scaled by monitor scale.
inline constexpr auto SUBSURFACES_RADIUS  = "plugin:hyprglass:subsurfaces:radius";

// Window background cache kill switch; commit-driven invalidation (single
// global bool, no per-namespace concept for windows) and its throttle —
// mirrors the layers:live_resample/live_resample_fps keys above.
inline constexpr auto WINDOWS_BACKGROUND_CACHE  = "plugin:hyprglass:windows:background_cache";
inline constexpr auto WINDOWS_LIVE_RESAMPLE     = "plugin:hyprglass:windows:live_resample";
inline constexpr auto WINDOWS_LIVE_RESAMPLE_FPS = "plugin:hyprglass:windows:live_resample_fps";

// Overridable — dark theme overrides
inline constexpr auto DARK_BLUR_STRENGTH        = "plugin:hyprglass:dark:blur_strength";
inline constexpr auto DARK_BLUR_ITERATIONS      = "plugin:hyprglass:dark:blur_iterations";
inline constexpr auto DARK_NOISE_STRENGTH       = "plugin:hyprglass:dark:noise_strength";
inline constexpr auto DARK_REFRACTION_STRENGTH  = "plugin:hyprglass:dark:refraction_strength";
inline constexpr auto DARK_CHROMATIC_ABERRATION = "plugin:hyprglass:dark:chromatic_aberration";
inline constexpr auto DARK_FRESNEL_STRENGTH     = "plugin:hyprglass:dark:fresnel_strength";
inline constexpr auto DARK_SPECULAR_STRENGTH    = "plugin:hyprglass:dark:specular_strength";
inline constexpr auto DARK_SPECULAR_ANGLE       = "plugin:hyprglass:dark:specular_angle";
inline constexpr auto DARK_GLASS_OPACITY        = "plugin:hyprglass:dark:glass_opacity";
inline constexpr auto DARK_EDGE_THICKNESS       = "plugin:hyprglass:dark:edge_thickness";
inline constexpr auto DARK_EDGE_THICKNESS_PX    = "plugin:hyprglass:dark:edge_thickness_px";
inline constexpr auto DARK_EDGE_HEIGHT          = "plugin:hyprglass:dark:edge_height";
inline constexpr auto DARK_FRESNEL_REFLECTION   = "plugin:hyprglass:dark:fresnel_reflection";
inline constexpr auto DARK_TINT_COLOR           = "plugin:hyprglass:dark:tint_color";
inline constexpr auto DARK_LENS_DISTORTION      = "plugin:hyprglass:dark:lens_distortion";
inline constexpr auto DARK_BRIGHTNESS           = "plugin:hyprglass:dark:brightness";
inline constexpr auto DARK_CONTRAST             = "plugin:hyprglass:dark:contrast";
inline constexpr auto DARK_SATURATION           = "plugin:hyprglass:dark:saturation";
inline constexpr auto DARK_VIBRANCY             = "plugin:hyprglass:dark:vibrancy";
inline constexpr auto DARK_VIBRANCY_DARKNESS    = "plugin:hyprglass:dark:vibrancy_darkness";
inline constexpr auto DARK_ADAPTIVE_DIM          = "plugin:hyprglass:dark:adaptive_dim";
inline constexpr auto DARK_ADAPTIVE_BOOST        = "plugin:hyprglass:dark:adaptive_boost";
inline constexpr auto DARK_REFRACTION_FLOW      = "plugin:hyprglass:dark:refraction_flow";
inline constexpr auto DARK_REFRACTION_SPREAD    = "plugin:hyprglass:dark:refraction_spread";
inline constexpr auto DARK_FRESNEL_TINT         = "plugin:hyprglass:dark:fresnel_tint";
inline constexpr auto DARK_BEVEL_STRENGTH       = "plugin:hyprglass:dark:bevel_strength";
inline constexpr auto DARK_BEVEL_SIZE           = "plugin:hyprglass:dark:bevel_size";
inline constexpr auto DARK_FRESNEL_COLOR        = "plugin:hyprglass:dark:fresnel_color";
inline constexpr auto DARK_BEVEL_COLOR          = "plugin:hyprglass:dark:bevel_color";
inline constexpr auto DARK_BEVEL_TINT           = "plugin:hyprglass:dark:bevel_tint";
inline constexpr auto DARK_BEVEL_ANGLE          = "plugin:hyprglass:dark:bevel_angle";
inline constexpr auto DARK_BEVEL_SHADOW         = "plugin:hyprglass:dark:bevel_shadow";
inline constexpr auto DARK_SELF_SAMPLE          = "plugin:hyprglass:dark:self_sample";
inline constexpr auto DARK_XRAY                 = "plugin:hyprglass:dark:xray";
inline constexpr auto DARK_FOCUS_LIGHT          = "plugin:hyprglass:dark:focus_light";
inline constexpr auto DARK_FOCUS_LIGHT_COLOR    = "plugin:hyprglass:dark:focus_light_color";
inline constexpr auto DARK_FROST                = "plugin:hyprglass:dark:frost";
inline constexpr auto DARK_FROST_BLUR           = "plugin:hyprglass:dark:frost_blur";

// Overridable — light theme overrides
inline constexpr auto LIGHT_BLUR_STRENGTH        = "plugin:hyprglass:light:blur_strength";
inline constexpr auto LIGHT_BLUR_ITERATIONS      = "plugin:hyprglass:light:blur_iterations";
inline constexpr auto LIGHT_NOISE_STRENGTH       = "plugin:hyprglass:light:noise_strength";
inline constexpr auto LIGHT_REFRACTION_STRENGTH  = "plugin:hyprglass:light:refraction_strength";
inline constexpr auto LIGHT_CHROMATIC_ABERRATION = "plugin:hyprglass:light:chromatic_aberration";
inline constexpr auto LIGHT_FRESNEL_STRENGTH     = "plugin:hyprglass:light:fresnel_strength";
inline constexpr auto LIGHT_SPECULAR_STRENGTH    = "plugin:hyprglass:light:specular_strength";
inline constexpr auto LIGHT_SPECULAR_ANGLE       = "plugin:hyprglass:light:specular_angle";
inline constexpr auto LIGHT_GLASS_OPACITY        = "plugin:hyprglass:light:glass_opacity";
inline constexpr auto LIGHT_EDGE_THICKNESS       = "plugin:hyprglass:light:edge_thickness";
inline constexpr auto LIGHT_EDGE_THICKNESS_PX    = "plugin:hyprglass:light:edge_thickness_px";
inline constexpr auto LIGHT_EDGE_HEIGHT          = "plugin:hyprglass:light:edge_height";
inline constexpr auto LIGHT_FRESNEL_REFLECTION   = "plugin:hyprglass:light:fresnel_reflection";
inline constexpr auto LIGHT_TINT_COLOR           = "plugin:hyprglass:light:tint_color";
inline constexpr auto LIGHT_LENS_DISTORTION      = "plugin:hyprglass:light:lens_distortion";
inline constexpr auto LIGHT_BRIGHTNESS           = "plugin:hyprglass:light:brightness";
inline constexpr auto LIGHT_CONTRAST             = "plugin:hyprglass:light:contrast";
inline constexpr auto LIGHT_SATURATION           = "plugin:hyprglass:light:saturation";
inline constexpr auto LIGHT_VIBRANCY             = "plugin:hyprglass:light:vibrancy";
inline constexpr auto LIGHT_VIBRANCY_DARKNESS    = "plugin:hyprglass:light:vibrancy_darkness";
inline constexpr auto LIGHT_ADAPTIVE_DIM          = "plugin:hyprglass:light:adaptive_dim";
inline constexpr auto LIGHT_ADAPTIVE_BOOST        = "plugin:hyprglass:light:adaptive_boost";
inline constexpr auto LIGHT_REFRACTION_FLOW      = "plugin:hyprglass:light:refraction_flow";
inline constexpr auto LIGHT_REFRACTION_SPREAD    = "plugin:hyprglass:light:refraction_spread";
inline constexpr auto LIGHT_FRESNEL_TINT         = "plugin:hyprglass:light:fresnel_tint";
inline constexpr auto LIGHT_BEVEL_STRENGTH       = "plugin:hyprglass:light:bevel_strength";
inline constexpr auto LIGHT_BEVEL_SIZE           = "plugin:hyprglass:light:bevel_size";
inline constexpr auto LIGHT_FRESNEL_COLOR        = "plugin:hyprglass:light:fresnel_color";
inline constexpr auto LIGHT_BEVEL_COLOR          = "plugin:hyprglass:light:bevel_color";
inline constexpr auto LIGHT_BEVEL_TINT           = "plugin:hyprglass:light:bevel_tint";
inline constexpr auto LIGHT_BEVEL_ANGLE          = "plugin:hyprglass:light:bevel_angle";
inline constexpr auto LIGHT_BEVEL_SHADOW         = "plugin:hyprglass:light:bevel_shadow";
inline constexpr auto LIGHT_SELF_SAMPLE          = "plugin:hyprglass:light:self_sample";
inline constexpr auto LIGHT_XRAY                 = "plugin:hyprglass:light:xray";
inline constexpr auto LIGHT_FOCUS_LIGHT          = "plugin:hyprglass:light:focus_light";
inline constexpr auto LIGHT_FOCUS_LIGHT_COLOR    = "plugin:hyprglass:light:focus_light_color";
inline constexpr auto LIGHT_FROST               = "plugin:hyprglass:light:frost";
inline constexpr auto LIGHT_FROST_BLUR          = "plugin:hyprglass:light:frost_blur";

} // namespace ConfigKeys

// Cached pointers for a single config layer (built-in dark/light/global)
struct SOverridableConfig {
    Config::FLOAT* const*   blurStrength        = nullptr;
    Config::INTEGER* const* blurIterations      = nullptr;
    Config::FLOAT* const*   noiseStrength       = nullptr;
    Config::FLOAT* const*   refractionStrength  = nullptr;
    Config::FLOAT* const*   chromaticAberration = nullptr;
    Config::FLOAT* const*   fresnelStrength     = nullptr;
    Config::FLOAT* const*   specularStrength    = nullptr;
    Config::FLOAT* const*   specularAngle       = nullptr;
    Config::FLOAT* const*   glassOpacity        = nullptr;
    Config::FLOAT* const*   edgeThickness       = nullptr;
    Config::FLOAT* const*   edgeThicknessPx     = nullptr;
    Config::FLOAT* const*   edgeHeight          = nullptr;
    Config::FLOAT* const*   fresnelReflection   = nullptr;
    Config::INTEGER* const* tintColor           = nullptr;
    Config::FLOAT* const*   lensDistortion      = nullptr;
    Config::FLOAT* const*   brightness          = nullptr;
    Config::FLOAT* const*   contrast            = nullptr;
    Config::FLOAT* const*   saturation          = nullptr;
    Config::FLOAT* const*   vibrancy            = nullptr;
    Config::FLOAT* const*   vibrancyDarkness    = nullptr;
    Config::FLOAT* const*   adaptiveDim         = nullptr;
    Config::FLOAT* const*   adaptiveBoost       = nullptr;
    Config::FLOAT* const*   refractionFlow      = nullptr;
    Config::FLOAT* const*   refractionSpread    = nullptr;
    Config::FLOAT* const*   fresnelTint         = nullptr;
    Config::FLOAT* const*   bevelStrength       = nullptr;
    Config::FLOAT* const*   bevelSize           = nullptr;
    Config::INTEGER* const* fresnelColor        = nullptr;
    Config::INTEGER* const* bevelColor          = nullptr;
    Config::FLOAT* const*   bevelTint           = nullptr;
    Config::FLOAT* const*   bevelAngle          = nullptr;
    Config::FLOAT* const*   bevelShadow         = nullptr;
    Config::FLOAT* const*   selfSample          = nullptr;
    Config::INTEGER* const* xray                = nullptr;
    Config::FLOAT* const*   focusLight          = nullptr;
    Config::INTEGER* const* focusLightColor     = nullptr;
    Config::FLOAT* const*   frost               = nullptr;
    Config::FLOAT* const*   frostBlur           = nullptr;
};

// Plain values for a user-defined preset layer (all sentinel = not set → inherit)
struct SPresetValues {
    float   blurStrength       = static_cast<float>(SENTINEL_FLOAT);
    int64_t blurIterations     = SENTINEL_INT;
    float   noiseStrength      = static_cast<float>(SENTINEL_FLOAT);
    float   refractionStrength = static_cast<float>(SENTINEL_FLOAT);
    float   chromaticAberration = static_cast<float>(SENTINEL_FLOAT);
    float   fresnelStrength    = static_cast<float>(SENTINEL_FLOAT);
    float   specularStrength   = static_cast<float>(SENTINEL_FLOAT);
    float   specularAngle      = static_cast<float>(SENTINEL_FLOAT);
    float   glassOpacity       = static_cast<float>(SENTINEL_FLOAT);
    float   edgeThickness      = static_cast<float>(SENTINEL_FLOAT);
    float   edgeThicknessPx    = static_cast<float>(SENTINEL_FLOAT);
    float   edgeHeight         = static_cast<float>(SENTINEL_FLOAT);
    float   fresnelReflection  = static_cast<float>(SENTINEL_FLOAT);
    int64_t tintColor          = SENTINEL_INT;
    float   lensDistortion     = static_cast<float>(SENTINEL_FLOAT);
    float   brightness         = static_cast<float>(SENTINEL_FLOAT);
    float   contrast           = static_cast<float>(SENTINEL_FLOAT);
    float   saturation         = static_cast<float>(SENTINEL_FLOAT);
    float   vibrancy           = static_cast<float>(SENTINEL_FLOAT);
    float   vibrancyDarkness   = static_cast<float>(SENTINEL_FLOAT);
    float   adaptiveDim        = static_cast<float>(SENTINEL_FLOAT);
    float   adaptiveBoost      = static_cast<float>(SENTINEL_FLOAT);
    float   refractionFlow     = static_cast<float>(SENTINEL_FLOAT);
    float   refractionSpread   = static_cast<float>(SENTINEL_FLOAT);
    float   fresnelTint        = static_cast<float>(SENTINEL_FLOAT);
    float   bevelStrength      = static_cast<float>(SENTINEL_FLOAT);
    float   bevelSize          = static_cast<float>(SENTINEL_FLOAT);
    int64_t fresnelColor       = SENTINEL_INT;
    int64_t bevelColor         = SENTINEL_INT;
    float   bevelTint          = static_cast<float>(SENTINEL_FLOAT);
    float   bevelAngle         = static_cast<float>(SENTINEL_FLOAT);
    float   bevelShadow        = static_cast<float>(SENTINEL_FLOAT);
    float   selfSample         = static_cast<float>(SENTINEL_FLOAT);
    int64_t xray               = SENTINEL_INT;
    float   focusLight         = static_cast<float>(SENTINEL_FLOAT);
    int64_t focusLightColor    = SENTINEL_INT;
    float   frost              = static_cast<float>(SENTINEL_FLOAT);
    float   frostBlur          = static_cast<float>(SENTINEL_FLOAT);
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

    return {};
}

// A Hyprland key stored as a bool: read through its reported type, so a key that
// reports anything else reads as unset instead of as a misread byte.
struct BoolConfigPtr {
    void* const*          dataptr = nullptr;
    const std::type_info* type    = nullptr;
};

inline bool readBoolConfig(const BoolConfigPtr& ptr) {
    if (!ptr.dataptr || !ptr.type || *ptr.type != typeid(bool))
        return false;

    return **reinterpret_cast<const bool* const*>(ptr.dataptr);
}

struct SPluginConfig {
    Config::INTEGER* const* enabled           = nullptr;
    // Glass replaces Hyprland's blur for glassed windows: when set, the plugin
    // marks them with the noblur window property so Hyprland composites them
    // against the live framebuffer (which contains the glass) instead of its
    // pre-frame cached blur.
    Config::INTEGER* const* manageWindowBlur  = nullptr;
    // Skip glass for windows CWindow::opaque() reports as opaque: nothing behind
    // them is visible, so sampling and blurring their background is wasted work.
    Config::INTEGER* const* skipOpaqueWindows = nullptr;
    // Derives a smaller blur pass count from the requested radius (GlassRenderer::
    // foldBlurPasses) instead of always running blur_iterations passes at full radius.
    Config::INTEGER* const* blurFold          = nullptr;
    // Hyprland's render:xp_mode: no wallpaper or bottom layers are drawn, so
    // there is nothing for x-ray to show.
    BoolConfigPtr           xpMode;
    // Hyprland's animations:enabled: off, the focus light switches without a fade.
    BoolConfigPtr           animationsEnabled;
    StringConfigPtr         defaultTheme;
    StringConfigPtr         defaultPreset;

    // Performance diagnostics (see Diagnostics.hpp for the hyprctl side)
    StringConfigPtr         debugMode;
    Config::INTEGER* const* debugTimers = nullptr;

    Config::INTEGER* const* layersEnabled                 = nullptr;
    StringConfigPtr         layersNamespaces;
    StringConfigPtr         layersExcludeNamespaces;
    StringConfigPtr         layersPreset;
    StringConfigPtr         layersNamespacePresets;
    Config::FLOAT* const*   layersMaskThreshold           = nullptr;
    StringConfigPtr         layersNamespaceMaskThresholds;
    StringConfigPtr         layersNamespaceLiveResample;
    Config::INTEGER* const* layersLiveResample            = nullptr;
    Config::INTEGER* const* layersLiveResampleFps         = nullptr;
    Config::INTEGER* const* layersForceLiveResample       = nullptr;
    StringConfigPtr         layersMaskMode;
    StringConfigPtr         layersNamespaceMaskModes;
    Config::FLOAT* const*   layersMaskFeather             = nullptr;
    StringConfigPtr         layersNamespaceMaskFeathers;
    StringConfigPtr         layersNamespaceShapes;
    Config::INTEGER* const* layersManageBlur              = nullptr;

    Config::INTEGER* const* subsurfacesEnabled = nullptr;
    StringConfigPtr         subsurfacesPreset;
    Config::FLOAT* const*   subsurfacesRadius  = nullptr;

    Config::INTEGER* const* windowsBackgroundCache = nullptr;
    Config::INTEGER* const* windowsLiveResample    = nullptr;
    Config::INTEGER* const* windowsLiveResampleFps = nullptr;

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
    Config::FLOAT* const* SOverridableConfig::* configField,
    float hardcodedDefault = static_cast<float>(SENTINEL_FLOAT));

[[nodiscard]] int64_t resolvePresetInt(
    const SResolveContext& context,
    int64_t SPresetValues::* presetField,
    Config::INTEGER* const* SOverridableConfig::* configField,
    int64_t hardcodedDefault = SENTINEL_INT);

// True when any tier can resolve self_sample above 0. Answers "could a window
// sample itself" without a window at hand, so the surface observer can stay
// unarmed on the default config.
[[nodiscard]] bool anySelfSampleConfigured(const SPluginConfig&                                  config,
                                           const std::unordered_map<std::string, SCustomPreset>& customPresets);

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
