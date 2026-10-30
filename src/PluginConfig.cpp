#include "PluginConfig.hpp"
#include "BuiltInPresets.hpp"
#include "Globals.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/lua/ConfigManager.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

// ── Config registration ──────────────────────────────────────────────────────

namespace {

template <typename T, typename Default>
static void addConfigValue(HANDLE handle, const char* name, Default defaultValue) {
    HyprlandAPI::addConfigValueV2(handle,
        Config::Values::makeConfigValue<T>(name, "", defaultValue, Config::Values::valueOptions_t<T>{}));
}

} // namespace

// Forward declarations for Lua handlers (need access to file-static preset/layer data below)
static int handleLuaPreset(lua_State* L);
static int handleLuaLayer(lua_State* L);
static int handleLuaConfig(lua_State* L);

std::optional<ELayerMaskMode> parseLayerMaskMode(std::string_view value) {
    if (value == "auto")   return ELayerMaskMode::AUTO;
    if (value == "alpha")  return ELayerMaskMode::ALPHA;
    if (value == "region") return ELayerMaskMode::REGION;
    if (value == "contour") return ELayerMaskMode::CONTOUR;
    return std::nullopt;
}

// Far beyond any monitor, but small enough that scaled sums and squares in the
// shader and the box maths stay finite.
constexpr double SHAPE_LIMIT_PX = 16384.0;

static float clampShapeInset(double value) {
    return static_cast<float>(std::clamp(value, -SHAPE_LIMIT_PX, SHAPE_LIMIT_PX));
}

// Any negative radius means a capsule.
static float clampShapeRadius(double value) {
    return value < 0.0 ? -1.0f : static_cast<float>(std::min(value, SHAPE_LIMIT_PX));
}

std::optional<EDebugMode> parseDebugMode(std::string_view value) {
    if (value == "off")          return EDebugMode::OFF;
    if (value == "hints_only")   return EDebugMode::HINTS_ONLY;
    if (value == "gl_work_only") return EDebugMode::GL_WORK_ONLY;
    return std::nullopt;
}

EDebugMode currentDebugMode() {
    if (!g_pGlobalState)
        return EDebugMode::OFF;
    return parseDebugMode(readStringConfig(g_pGlobalState->config.debugMode)).value_or(EDebugMode::OFF);
}

void registerConfig(HANDLE handle) {
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::ENABLED, true);
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::MANAGE_WINDOW_BLUR, true);
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::SKIP_OPAQUE_WINDOWS, true);
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::BLUR_FOLD, true);
    addConfigValue<Config::Values::String>(handle, ConfigKeys::DEFAULT_THEME, Config::STRING{"dark"});
    addConfigValue<Config::Values::String>(handle, ConfigKeys::DEFAULT_PRESET, Config::STRING{"default"});

    // Performance diagnostics
    addConfigValue<Config::Values::String>(handle, ConfigKeys::DEBUG_MODE, Config::STRING{"off"});
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::DEBUG_TIMERS, false);

    // Layer surface support
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::LAYERS_ENABLED, false);
    addConfigValue<Config::Values::String>(handle, ConfigKeys::LAYERS_PRESET, Config::STRING{});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LAYERS_MASK_THRESHOLD, Config::FLOAT{0.001});
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::LAYERS_LIVE_RESAMPLE, true);
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::LAYERS_LIVE_RESAMPLE_FPS, Config::INTEGER{30});
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::LAYERS_FORCE_LIVE_RESAMPLE, false);
    addConfigValue<Config::Values::String>(handle, ConfigKeys::LAYERS_MASK_MODE, Config::STRING{"auto"});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LAYERS_MASK_FEATHER, Config::FLOAT{0.0});
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::LAYERS_MANAGE_BLUR, true);

    // Subsurface item glass
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::SUBSURFACES_ENABLED, false);
    addConfigValue<Config::Values::String>(handle, ConfigKeys::SUBSURFACES_PRESET, Config::STRING{});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::SUBSURFACES_RADIUS, Config::FLOAT{-1.0});

    // Window background cache
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::WINDOWS_BACKGROUND_CACHE, true);
    addConfigValue<Config::Values::Bool>(handle, ConfigKeys::WINDOWS_LIVE_RESAMPLE, true);
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::WINDOWS_LIVE_RESAMPLE_FPS, Config::INTEGER{30});

    // Global level — real defaults for effect settings,
    // sentinel for theme-sensitive settings (fallback to hardcoded theme defaults)
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::BLUR_STRENGTH, Config::FLOAT{GlobalDefaults::BLUR_STRENGTH});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::BLUR_ITERATIONS, Config::INTEGER{GlobalDefaults::BLUR_ITERATIONS});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::NOISE_STRENGTH, Config::FLOAT{GlobalDefaults::NOISE_STRENGTH});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::REFRACTION_STRENGTH, Config::FLOAT{GlobalDefaults::REFRACTION_STRENGTH});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::CHROMATIC_ABERRATION, Config::FLOAT{GlobalDefaults::CHROMATIC_ABERRATION});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::FRESNEL_STRENGTH, Config::FLOAT{GlobalDefaults::FRESNEL_STRENGTH});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::SPECULAR_STRENGTH, Config::FLOAT{GlobalDefaults::SPECULAR_STRENGTH});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::SPECULAR_ANGLE, Config::FLOAT{GlobalDefaults::SPECULAR_ANGLE});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::GLASS_OPACITY, Config::FLOAT{GlobalDefaults::GLASS_OPACITY});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::EDGE_THICKNESS, Config::FLOAT{GlobalDefaults::EDGE_THICKNESS});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::EDGE_THICKNESS_PX, Config::FLOAT{GlobalDefaults::EDGE_THICKNESS_PX});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::EDGE_HEIGHT, Config::FLOAT{GlobalDefaults::EDGE_HEIGHT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::FRESNEL_REFLECTION, Config::FLOAT{GlobalDefaults::FRESNEL_REFLECTION});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::TINT_COLOR, Config::INTEGER{GlobalDefaults::TINT_COLOR});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LENS_DISTORTION, Config::FLOAT{GlobalDefaults::LENS_DISTORTION});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::BRIGHTNESS, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::CONTRAST, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::SATURATION, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::VIBRANCY, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::VIBRANCY_DARKNESS, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::ADAPTIVE_DIM, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::ADAPTIVE_BOOST, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::REFRACTION_FLOW, Config::FLOAT{GlobalDefaults::REFRACTION_FLOW});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::REFRACTION_SPREAD, Config::FLOAT{GlobalDefaults::REFRACTION_SPREAD});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::FRESNEL_TINT, Config::FLOAT{GlobalDefaults::FRESNEL_TINT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::BEVEL_STRENGTH, Config::FLOAT{GlobalDefaults::BEVEL_STRENGTH});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::BEVEL_SIZE, Config::FLOAT{GlobalDefaults::BEVEL_SIZE});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::FRESNEL_COLOR, Config::INTEGER{GlobalDefaults::FRESNEL_COLOR});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::BEVEL_COLOR, Config::INTEGER{GlobalDefaults::BEVEL_COLOR});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::BEVEL_TINT, Config::FLOAT{GlobalDefaults::BEVEL_TINT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::BEVEL_ANGLE, Config::FLOAT{GlobalDefaults::BEVEL_ANGLE});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::BEVEL_SHADOW, Config::FLOAT{GlobalDefaults::BEVEL_SHADOW});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::SELF_SAMPLE, Config::FLOAT{GlobalDefaults::SELF_SAMPLE});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::XRAY, Config::INTEGER{GlobalDefaults::XRAY});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::FOCUS_LIGHT, Config::FLOAT{GlobalDefaults::FOCUS_LIGHT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::FOCUS_LIGHT_COLOR, Config::INTEGER{GlobalDefaults::FOCUS_LIGHT_COLOR});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::FROST, Config::FLOAT{GlobalDefaults::FROST});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::FROST_BLUR, Config::FLOAT{GlobalDefaults::FROST_BLUR});

    // Dark theme overrides — all sentinel (inherit from global)
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_BLUR_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::DARK_BLUR_ITERATIONS, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_NOISE_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_REFRACTION_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_CHROMATIC_ABERRATION, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_FRESNEL_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_SPECULAR_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_SPECULAR_ANGLE, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_GLASS_OPACITY, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_EDGE_THICKNESS, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_EDGE_THICKNESS_PX, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_EDGE_HEIGHT, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_FRESNEL_REFLECTION, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::DARK_TINT_COLOR, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_LENS_DISTORTION, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_BRIGHTNESS, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_CONTRAST, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_SATURATION, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_VIBRANCY, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_VIBRANCY_DARKNESS, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_ADAPTIVE_DIM, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_ADAPTIVE_BOOST, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_REFRACTION_FLOW, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_REFRACTION_SPREAD, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_FRESNEL_TINT, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_BEVEL_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_BEVEL_SIZE, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::DARK_FRESNEL_COLOR, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::DARK_BEVEL_COLOR, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_BEVEL_TINT, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_BEVEL_ANGLE, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_BEVEL_SHADOW, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_SELF_SAMPLE, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::DARK_XRAY, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_FOCUS_LIGHT, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::DARK_FOCUS_LIGHT_COLOR, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_FROST, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::DARK_FROST_BLUR, Config::FLOAT{SENTINEL_FLOAT});

    // Light theme overrides — all sentinel (inherit from global)
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_BLUR_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::LIGHT_BLUR_ITERATIONS, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_NOISE_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_REFRACTION_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_CHROMATIC_ABERRATION, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_FRESNEL_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_SPECULAR_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_SPECULAR_ANGLE, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_GLASS_OPACITY, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_EDGE_THICKNESS, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_EDGE_THICKNESS_PX, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_EDGE_HEIGHT, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_FRESNEL_REFLECTION, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::LIGHT_TINT_COLOR, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_LENS_DISTORTION, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_BRIGHTNESS, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_CONTRAST, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_SATURATION, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_VIBRANCY, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_VIBRANCY_DARKNESS, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_ADAPTIVE_DIM, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_ADAPTIVE_BOOST, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_REFRACTION_FLOW, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_REFRACTION_SPREAD, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_FRESNEL_TINT, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_BEVEL_STRENGTH, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_BEVEL_SIZE, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::LIGHT_FRESNEL_COLOR, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::LIGHT_BEVEL_COLOR, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_BEVEL_TINT, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_BEVEL_ANGLE, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_BEVEL_SHADOW, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_SELF_SAMPLE, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::LIGHT_XRAY, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_FOCUS_LIGHT, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Int>(handle, ConfigKeys::LIGHT_FOCUS_LIGHT_COLOR, Config::INTEGER{SENTINEL_INT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_FROST, Config::FLOAT{SENTINEL_FLOAT});
    addConfigValue<Config::Values::Float>(handle, ConfigKeys::LIGHT_FROST_BLUR, Config::FLOAT{SENTINEL_FLOAT});

    // Lua callbacks for custom presets, layers and config.
    HyprlandAPI::addLuaFunction(handle, "hyprglass", "preset", handleLuaPreset);
    HyprlandAPI::addLuaFunction(handle, "hyprglass", "layer", handleLuaLayer);
    HyprlandAPI::addLuaFunction(handle, "hyprglass", "config", handleLuaConfig);
}

// ── Config pointer initialization ────────────────────────────────────────────

template <typename T>
static auto* getStaticPtr(HANDLE /*handle*/, const char* key) {
    return reinterpret_cast<T* const*>(Config::mgr()->getConfigValue(key).dataptr);
}

static StringConfigPtr getStringPtr(HANDLE /*handle*/, const char* key) {
    const auto value = Config::mgr()->getConfigValue(key);
    return {.dataptr = value.dataptr, .type = value.type};
}

static BoolConfigPtr getBoolPtr(HANDLE /*handle*/, const char* key) {
    const auto value = Config::mgr()->getConfigValue(key);
    return {.dataptr = value.dataptr, .type = value.type};
}

static void initOverridablePointers(HANDLE handle, SOverridableConfig& layer,
                                    const char* blurStrength, const char* blurIterations,
                                    const char* noiseStrength,
                                    const char* refractionStrength, const char* chromaticAberration,
                                    const char* fresnelStrength, const char* specularStrength,
                                    const char* specularAngle,
                                    const char* glassOpacity, const char* edgeThickness,
                                    const char* edgeThicknessPx,
                                    const char* edgeHeight, const char* fresnelReflection,
                                    const char* tintColor, const char* lensDistortion,
                                    const char* brightness, const char* contrast,
                                    const char* saturation, const char* vibrancy,
                                    const char* vibrancyDarkness, const char* adaptiveDim,
                                    const char* adaptiveBoost,
                                    const char* refractionFlow, const char* refractionSpread,
                                    const char* fresnelTint, const char* bevelStrength,
                                    const char* bevelSize, const char* fresnelColor,
                                    const char* bevelColor, const char* bevelTint,
                                    const char* bevelAngle, const char* bevelShadow,
                                    const char* selfSample, const char* xray,
                                    const char* focusLight, const char* focusLightColor,
                                    const char* frost, const char* frostBlur) {
    layer.blurStrength        = getStaticPtr<Config::FLOAT>(handle, blurStrength);
    layer.blurIterations      = getStaticPtr<Config::INTEGER>(handle, blurIterations);
    layer.noiseStrength       = getStaticPtr<Config::FLOAT>(handle, noiseStrength);
    layer.refractionStrength  = getStaticPtr<Config::FLOAT>(handle, refractionStrength);
    layer.chromaticAberration = getStaticPtr<Config::FLOAT>(handle, chromaticAberration);
    layer.fresnelStrength     = getStaticPtr<Config::FLOAT>(handle, fresnelStrength);
    layer.specularStrength    = getStaticPtr<Config::FLOAT>(handle, specularStrength);
    layer.specularAngle       = getStaticPtr<Config::FLOAT>(handle, specularAngle);
    layer.glassOpacity        = getStaticPtr<Config::FLOAT>(handle, glassOpacity);
    layer.edgeThickness       = getStaticPtr<Config::FLOAT>(handle, edgeThickness);
    layer.edgeThicknessPx     = getStaticPtr<Config::FLOAT>(handle, edgeThicknessPx);
    layer.edgeHeight          = getStaticPtr<Config::FLOAT>(handle, edgeHeight);
    layer.fresnelReflection   = getStaticPtr<Config::FLOAT>(handle, fresnelReflection);
    layer.tintColor           = getStaticPtr<Config::INTEGER>(handle, tintColor);
    layer.lensDistortion      = getStaticPtr<Config::FLOAT>(handle, lensDistortion);
    layer.brightness          = getStaticPtr<Config::FLOAT>(handle, brightness);
    layer.contrast            = getStaticPtr<Config::FLOAT>(handle, contrast);
    layer.saturation          = getStaticPtr<Config::FLOAT>(handle, saturation);
    layer.vibrancy            = getStaticPtr<Config::FLOAT>(handle, vibrancy);
    layer.vibrancyDarkness    = getStaticPtr<Config::FLOAT>(handle, vibrancyDarkness);
    layer.adaptiveDim         = getStaticPtr<Config::FLOAT>(handle, adaptiveDim);
    layer.adaptiveBoost       = getStaticPtr<Config::FLOAT>(handle, adaptiveBoost);
    layer.refractionFlow      = getStaticPtr<Config::FLOAT>(handle, refractionFlow);
    layer.refractionSpread    = getStaticPtr<Config::FLOAT>(handle, refractionSpread);
    layer.fresnelTint         = getStaticPtr<Config::FLOAT>(handle, fresnelTint);
    layer.bevelStrength       = getStaticPtr<Config::FLOAT>(handle, bevelStrength);
    layer.bevelSize           = getStaticPtr<Config::FLOAT>(handle, bevelSize);
    layer.fresnelColor        = getStaticPtr<Config::INTEGER>(handle, fresnelColor);
    layer.bevelColor          = getStaticPtr<Config::INTEGER>(handle, bevelColor);
    layer.bevelTint           = getStaticPtr<Config::FLOAT>(handle, bevelTint);
    layer.bevelAngle          = getStaticPtr<Config::FLOAT>(handle, bevelAngle);
    layer.bevelShadow         = getStaticPtr<Config::FLOAT>(handle, bevelShadow);
    layer.selfSample          = getStaticPtr<Config::FLOAT>(handle, selfSample);
    layer.xray                = getStaticPtr<Config::INTEGER>(handle, xray);
    layer.focusLight          = getStaticPtr<Config::FLOAT>(handle, focusLight);
    layer.focusLightColor     = getStaticPtr<Config::INTEGER>(handle, focusLightColor);
    layer.frost               = getStaticPtr<Config::FLOAT>(handle, frost);
    layer.frostBlur           = getStaticPtr<Config::FLOAT>(handle, frostBlur);
}

void initConfigPointers(HANDLE handle, SPluginConfig& config) {
    config.enabled           = getStaticPtr<Config::BOOL>(handle, ConfigKeys::ENABLED);
    config.manageWindowBlur  = getStaticPtr<Config::BOOL>(handle, ConfigKeys::MANAGE_WINDOW_BLUR);
    config.skipOpaqueWindows = getStaticPtr<Config::BOOL>(handle, ConfigKeys::SKIP_OPAQUE_WINDOWS);
    config.blurFold          = getStaticPtr<Config::BOOL>(handle, ConfigKeys::BLUR_FOLD);
    config.xpMode            = getBoolPtr(handle, "render:xp_mode");
    config.animationsEnabled = getBoolPtr(handle, "animations:enabled");
    config.defaultTheme  = getStringPtr(handle, ConfigKeys::DEFAULT_THEME);
    config.defaultPreset = getStringPtr(handle, ConfigKeys::DEFAULT_PRESET);

    config.debugMode   = getStringPtr(handle, ConfigKeys::DEBUG_MODE);
    config.debugTimers = getStaticPtr<Config::BOOL>(handle, ConfigKeys::DEBUG_TIMERS);

    config.layersEnabled           = getStaticPtr<Config::BOOL>(handle, ConfigKeys::LAYERS_ENABLED);
    config.layersPreset            = getStringPtr(handle, ConfigKeys::LAYERS_PRESET);
    config.layersMaskThreshold     = getStaticPtr<Config::FLOAT>(handle, ConfigKeys::LAYERS_MASK_THRESHOLD);
    config.layersLiveResample      = getStaticPtr<Config::BOOL>(handle, ConfigKeys::LAYERS_LIVE_RESAMPLE);
    config.layersLiveResampleFps   = getStaticPtr<Config::INTEGER>(handle, ConfigKeys::LAYERS_LIVE_RESAMPLE_FPS);
    config.layersForceLiveResample = getStaticPtr<Config::BOOL>(handle, ConfigKeys::LAYERS_FORCE_LIVE_RESAMPLE);
    config.layersMaskMode          = getStringPtr(handle, ConfigKeys::LAYERS_MASK_MODE);
    config.layersMaskFeather       = getStaticPtr<Config::FLOAT>(handle, ConfigKeys::LAYERS_MASK_FEATHER);
    config.layersManageBlur        = getStaticPtr<Config::BOOL>(handle, ConfigKeys::LAYERS_MANAGE_BLUR);

    config.subsurfacesEnabled = getStaticPtr<Config::BOOL>(handle, ConfigKeys::SUBSURFACES_ENABLED);
    config.subsurfacesPreset  = getStringPtr(handle, ConfigKeys::SUBSURFACES_PRESET);
    config.subsurfacesRadius  = getStaticPtr<Config::FLOAT>(handle, ConfigKeys::SUBSURFACES_RADIUS);

    config.windowsBackgroundCache = getStaticPtr<Config::BOOL>(handle, ConfigKeys::WINDOWS_BACKGROUND_CACHE);
    config.windowsLiveResample    = getStaticPtr<Config::BOOL>(handle, ConfigKeys::WINDOWS_LIVE_RESAMPLE);
    config.windowsLiveResampleFps = getStaticPtr<Config::INTEGER>(handle, ConfigKeys::WINDOWS_LIVE_RESAMPLE_FPS);

    initOverridablePointers(handle, config.global,
        ConfigKeys::BLUR_STRENGTH, ConfigKeys::BLUR_ITERATIONS, ConfigKeys::NOISE_STRENGTH,
        ConfigKeys::REFRACTION_STRENGTH, ConfigKeys::CHROMATIC_ABERRATION,
        ConfigKeys::FRESNEL_STRENGTH, ConfigKeys::SPECULAR_STRENGTH,
        ConfigKeys::SPECULAR_ANGLE,
        ConfigKeys::GLASS_OPACITY, ConfigKeys::EDGE_THICKNESS,
        ConfigKeys::EDGE_THICKNESS_PX,
        ConfigKeys::EDGE_HEIGHT, ConfigKeys::FRESNEL_REFLECTION,
        ConfigKeys::TINT_COLOR, ConfigKeys::LENS_DISTORTION,
        ConfigKeys::BRIGHTNESS, ConfigKeys::CONTRAST,
        ConfigKeys::SATURATION, ConfigKeys::VIBRANCY,
        ConfigKeys::VIBRANCY_DARKNESS, ConfigKeys::ADAPTIVE_DIM,
        ConfigKeys::ADAPTIVE_BOOST,
        ConfigKeys::REFRACTION_FLOW, ConfigKeys::REFRACTION_SPREAD,
        ConfigKeys::FRESNEL_TINT, ConfigKeys::BEVEL_STRENGTH,
        ConfigKeys::BEVEL_SIZE, ConfigKeys::FRESNEL_COLOR,
        ConfigKeys::BEVEL_COLOR, ConfigKeys::BEVEL_TINT,
        ConfigKeys::BEVEL_ANGLE, ConfigKeys::BEVEL_SHADOW,
        ConfigKeys::SELF_SAMPLE, ConfigKeys::XRAY,
        ConfigKeys::FOCUS_LIGHT, ConfigKeys::FOCUS_LIGHT_COLOR,
        ConfigKeys::FROST, ConfigKeys::FROST_BLUR);

    initOverridablePointers(handle, config.dark,
        ConfigKeys::DARK_BLUR_STRENGTH, ConfigKeys::DARK_BLUR_ITERATIONS, ConfigKeys::DARK_NOISE_STRENGTH,
        ConfigKeys::DARK_REFRACTION_STRENGTH, ConfigKeys::DARK_CHROMATIC_ABERRATION,
        ConfigKeys::DARK_FRESNEL_STRENGTH, ConfigKeys::DARK_SPECULAR_STRENGTH,
        ConfigKeys::DARK_SPECULAR_ANGLE,
        ConfigKeys::DARK_GLASS_OPACITY, ConfigKeys::DARK_EDGE_THICKNESS,
        ConfigKeys::DARK_EDGE_THICKNESS_PX,
        ConfigKeys::DARK_EDGE_HEIGHT, ConfigKeys::DARK_FRESNEL_REFLECTION,
        ConfigKeys::DARK_TINT_COLOR, ConfigKeys::DARK_LENS_DISTORTION,
        ConfigKeys::DARK_BRIGHTNESS, ConfigKeys::DARK_CONTRAST,
        ConfigKeys::DARK_SATURATION, ConfigKeys::DARK_VIBRANCY,
        ConfigKeys::DARK_VIBRANCY_DARKNESS, ConfigKeys::DARK_ADAPTIVE_DIM,
        ConfigKeys::DARK_ADAPTIVE_BOOST,
        ConfigKeys::DARK_REFRACTION_FLOW, ConfigKeys::DARK_REFRACTION_SPREAD,
        ConfigKeys::DARK_FRESNEL_TINT, ConfigKeys::DARK_BEVEL_STRENGTH,
        ConfigKeys::DARK_BEVEL_SIZE, ConfigKeys::DARK_FRESNEL_COLOR,
        ConfigKeys::DARK_BEVEL_COLOR, ConfigKeys::DARK_BEVEL_TINT,
        ConfigKeys::DARK_BEVEL_ANGLE, ConfigKeys::DARK_BEVEL_SHADOW,
        ConfigKeys::DARK_SELF_SAMPLE, ConfigKeys::DARK_XRAY,
        ConfigKeys::DARK_FOCUS_LIGHT, ConfigKeys::DARK_FOCUS_LIGHT_COLOR,
        ConfigKeys::DARK_FROST, ConfigKeys::DARK_FROST_BLUR);

    initOverridablePointers(handle, config.light,
        ConfigKeys::LIGHT_BLUR_STRENGTH, ConfigKeys::LIGHT_BLUR_ITERATIONS, ConfigKeys::LIGHT_NOISE_STRENGTH,
        ConfigKeys::LIGHT_REFRACTION_STRENGTH, ConfigKeys::LIGHT_CHROMATIC_ABERRATION,
        ConfigKeys::LIGHT_FRESNEL_STRENGTH, ConfigKeys::LIGHT_SPECULAR_STRENGTH,
        ConfigKeys::LIGHT_SPECULAR_ANGLE,
        ConfigKeys::LIGHT_GLASS_OPACITY, ConfigKeys::LIGHT_EDGE_THICKNESS,
        ConfigKeys::LIGHT_EDGE_THICKNESS_PX,
        ConfigKeys::LIGHT_EDGE_HEIGHT, ConfigKeys::LIGHT_FRESNEL_REFLECTION,
        ConfigKeys::LIGHT_TINT_COLOR, ConfigKeys::LIGHT_LENS_DISTORTION,
        ConfigKeys::LIGHT_BRIGHTNESS, ConfigKeys::LIGHT_CONTRAST,
        ConfigKeys::LIGHT_SATURATION, ConfigKeys::LIGHT_VIBRANCY,
        ConfigKeys::LIGHT_VIBRANCY_DARKNESS, ConfigKeys::LIGHT_ADAPTIVE_DIM,
        ConfigKeys::LIGHT_ADAPTIVE_BOOST,
        ConfigKeys::LIGHT_REFRACTION_FLOW, ConfigKeys::LIGHT_REFRACTION_SPREAD,
        ConfigKeys::LIGHT_FRESNEL_TINT, ConfigKeys::LIGHT_BEVEL_STRENGTH,
        ConfigKeys::LIGHT_BEVEL_SIZE, ConfigKeys::LIGHT_FRESNEL_COLOR,
        ConfigKeys::LIGHT_BEVEL_COLOR, ConfigKeys::LIGHT_BEVEL_TINT,
        ConfigKeys::LIGHT_BEVEL_ANGLE, ConfigKeys::LIGHT_BEVEL_SHADOW,
        ConfigKeys::LIGHT_SELF_SAMPLE, ConfigKeys::LIGHT_XRAY,
        ConfigKeys::LIGHT_FOCUS_LIGHT, ConfigKeys::LIGHT_FOCUS_LIGHT_COLOR,
        ConfigKeys::LIGHT_FROST, ConfigKeys::LIGHT_FROST_BLUR);
}

// ── Preset values ────────────────────────────────────────────────────────────

// Presets built during config parse, swapped into g_pGlobalState on configReloaded
static std::unordered_map<std::string, SCustomPreset> s_pendingPresets;

static float* presetFloatField(SPresetValues& values, std::string_view key) {
    if (key == "blur_strength")        return &values.blurStrength;
    if (key == "noise_strength")       return &values.noiseStrength;
    if (key == "refraction_strength")  return &values.refractionStrength;
    if (key == "chromatic_aberration") return &values.chromaticAberration;
    if (key == "fresnel_strength")     return &values.fresnelStrength;
    if (key == "specular_strength")    return &values.specularStrength;
    if (key == "specular_angle")       return &values.specularAngle;
    if (key == "glass_opacity")        return &values.glassOpacity;
    if (key == "edge_thickness")       return &values.edgeThickness;
    if (key == "edge_thickness_px")    return &values.edgeThicknessPx;
    if (key == "edge_height")          return &values.edgeHeight;
    if (key == "fresnel_reflection")   return &values.fresnelReflection;
    if (key == "lens_distortion")      return &values.lensDistortion;
    if (key == "brightness")           return &values.brightness;
    if (key == "contrast")             return &values.contrast;
    if (key == "saturation")           return &values.saturation;
    if (key == "vibrancy")             return &values.vibrancy;
    if (key == "vibrancy_darkness")    return &values.vibrancyDarkness;
    if (key == "adaptive_dim")         return &values.adaptiveDim;
    if (key == "adaptive_boost")       return &values.adaptiveBoost;
    if (key == "refraction_flow")      return &values.refractionFlow;
    if (key == "refraction_spread")    return &values.refractionSpread;
    if (key == "fresnel_tint")         return &values.fresnelTint;
    if (key == "bevel_strength")       return &values.bevelStrength;
    if (key == "bevel_size")           return &values.bevelSize;
    if (key == "bevel_tint")           return &values.bevelTint;
    if (key == "bevel_angle")          return &values.bevelAngle;
    if (key == "bevel_shadow")         return &values.bevelShadow;
    if (key == "self_sample")          return &values.selfSample;
    if (key == "focus_light")          return &values.focusLight;
    if (key == "frost")                return &values.frost;
    if (key == "frost_blur")           return &values.frostBlur;
    return nullptr;
}

static int64_t* presetIntField(SPresetValues& values, std::string_view key) {
    if (key == "blur_iterations") return &values.blurIterations;
    if (key == "tint_color")      return &values.tintColor;
    if (key == "fresnel_color")   return &values.fresnelColor;
    if (key == "bevel_color")     return &values.bevelColor;
    if (key == "xray")            return &values.xray;
    if (key == "focus_light_color") return &values.focusLightColor;
    return nullptr;
}

static void mergePresetValues(SPresetValues& target, const SPresetValues& overrides) {
    auto mergeFloat = [](float& dst, float src) { if (src >= 0.0f) dst = src; };
    auto mergeInt   = [](int64_t& dst, int64_t src) { if (src >= 0) dst = src; };

    mergeFloat(target.blurStrength, overrides.blurStrength);
    mergeInt(target.blurIterations, overrides.blurIterations);
    mergeFloat(target.noiseStrength, overrides.noiseStrength);
    mergeFloat(target.refractionStrength, overrides.refractionStrength);
    mergeFloat(target.chromaticAberration, overrides.chromaticAberration);
    mergeFloat(target.fresnelStrength, overrides.fresnelStrength);
    mergeFloat(target.specularStrength, overrides.specularStrength);
    mergeFloat(target.specularAngle, overrides.specularAngle);
    mergeFloat(target.glassOpacity, overrides.glassOpacity);
    mergeFloat(target.edgeThickness, overrides.edgeThickness);
    mergeFloat(target.edgeThicknessPx, overrides.edgeThicknessPx);
    mergeFloat(target.edgeHeight, overrides.edgeHeight);
    mergeFloat(target.fresnelReflection, overrides.fresnelReflection);
    mergeInt(target.tintColor, overrides.tintColor);
    mergeFloat(target.lensDistortion, overrides.lensDistortion);
    mergeFloat(target.brightness, overrides.brightness);
    mergeFloat(target.contrast, overrides.contrast);
    mergeFloat(target.saturation, overrides.saturation);
    mergeFloat(target.vibrancy, overrides.vibrancy);
    mergeFloat(target.vibrancyDarkness, overrides.vibrancyDarkness);
    mergeFloat(target.adaptiveDim, overrides.adaptiveDim);
    mergeFloat(target.adaptiveBoost, overrides.adaptiveBoost);
    mergeFloat(target.refractionFlow, overrides.refractionFlow);
    mergeFloat(target.refractionSpread, overrides.refractionSpread);
    mergeFloat(target.fresnelTint, overrides.fresnelTint);
    mergeFloat(target.bevelStrength, overrides.bevelStrength);
    mergeFloat(target.bevelSize, overrides.bevelSize);
    mergeInt(target.fresnelColor, overrides.fresnelColor);
    mergeInt(target.bevelColor, overrides.bevelColor);
    mergeFloat(target.bevelTint, overrides.bevelTint);
    mergeFloat(target.bevelAngle, overrides.bevelAngle);
    mergeFloat(target.bevelShadow, overrides.bevelShadow);
    mergeFloat(target.selfSample, overrides.selfSample);
    mergeInt(target.xray, overrides.xray);
    mergeFloat(target.focusLight, overrides.focusLight);
    mergeInt(target.focusLightColor, overrides.focusLightColor);
    mergeFloat(target.frost, overrides.frost);
    mergeFloat(target.frostBlur, overrides.frostBlur);
}

// Set by every hyprglass.* call, cleared before each reload.
static bool s_hyprglassCalledThisReload = false;

// A reload that fails its syntax check emits config.reloaded without resetting the config values
// or running any hyprglass.* call: committing the empty pending lists would wipe what the last good
// reload set. A reload that ran the user's calls commits them even if it ended in an error, as
// the values it applied stay applied.
static bool keepCommittedState() {
    return !s_hyprglassCalledThisReload && !Config::mgr()->configVerifPassed();
}

static void clearPendingPresets() {
    s_pendingPresets.clear();
}

void commitPendingPresets() {
    if (!g_pGlobalState) return;

    // customPresets always holds the built-ins once committed; empty means nothing to keep yet
    if (!g_pGlobalState->customPresets.empty() && keepCommittedState())
        return;

    // Start with built-in presets, then merge user-defined overrides (non-sentinel fields win)
    auto merged = BuiltInPresets::getAll();
    for (auto& [name, userPreset] : s_pendingPresets) {
        if (auto it = merged.find(name); it != merged.end()) {
            if (!userPreset.inherits.empty())
                it->second.inherits = userPreset.inherits;
            mergePresetValues(it->second.shared, userPreset.shared);
            mergePresetValues(it->second.dark, userPreset.dark);
            mergePresetValues(it->second.light, userPreset.light);
        } else {
            merged[name] = std::move(userPreset);
        }
    }

    g_pGlobalState->customPresets = std::move(merged);
    s_pendingPresets.clear();
}

// ── Lua config handler ──────────────────────────────────────────────────────
// hyprglass.config({...}) forwards to a single hl.config({ plugin = { hyprglass = ... } }).
// Keys are validated first so unknown options are reported with the user's file and line instead of '[C]'.
// Lua errors longjmp, so they are raised only while no C++ object lives in our frames.

// The Lua manager is the only config manager, and these callbacks run from its Lua state.
static Config::Lua::CConfigManager& luaConfigManager() {
    return static_cast<Config::Lua::CConfigManager&>(*Config::mgr());
}

// "file:line" of the user's code calling the running hyprglass.* function (stack level 1), empty when unknown.
static std::string callerLocation(lua_State* L) {
    lua_Debug ar{};
    if (!lua_getstack(L, 1, &ar) || !lua_getinfo(L, "Sl", &ar) || ar.currentline <= 0)
        return {};

    // A file chunk is named "@path"; a hyprctl eval chunk is named by its whole code, which short_src truncates
    const char* source = ar.source && *ar.source == '@' ? ar.source + 1 : ar.short_src;
    return std::format("{}:{}", source, ar.currentline);
}

// For hyprglass.preset and hyprglass.layer, whose C++ objects a Lua error would unwind past:
// the message goes to the error list `hyprctl configerrors` shows and the call carries on.
static void reportLuaError(lua_State* L, std::string_view function, const std::string& message) {
    const std::string where = callerLocation(L);
    luaConfigManager().addError(std::format("{}{}hyprglass.{}: {}", where, where.empty() ? "" : ": ", function, message));
}

struct SLuaConfigContext {
    lua_State*                                       L;
    Config::Lua::CConfigManager&                     luaMgr;
    std::string                                      where;
    std::vector<std::pair<std::string, std::string>> applied; // {option path, dotted key}

    void report(const std::string& msg) const {
        luaMgr.addError(where.empty() ? "hyprglass.config: " + msg : where + ": hyprglass.config: " + msg);
    }
};

// Copies the known options of the table at srcIdx into the table at dstIdx (absolute indices).
static void copyValidatedTable(SLuaConfigContext& ctx, int srcIdx, int dstIdx, const std::string& dottedPrefix, int depth = 0) {
    lua_State* L = ctx.L;
    // options are at most two levels deep; the cap also ends self-referencing tables
    if (depth > 4 || !lua_checkstack(L, 5)) {
        ctx.report(dottedPrefix.empty() ? std::string{"nesting too deep"} : std::format("nesting too deep in '{}'", dottedPrefix));
        return;
    }

    lua_pushnil(L);
    while (lua_next(L, srcIdx) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            ctx.report(dottedPrefix.empty() ? std::string{"ignoring non-string key"} : std::format("ignoring non-string key in '{}'", dottedPrefix));
            lua_pop(L, 1);
            continue;
        }

        // same normalisation as Hyprland's luaConfigValueName
        std::string key = lua_tostring(L, -2);
        std::replace(key.begin(), key.end(), ':', '.');
        std::replace(key.begin(), key.end(), '-', '_');
        const std::string dotted = dottedPrefix.empty() ? key : dottedPrefix + "." + key;

        std::string optionPath = "plugin:hyprglass:" + dotted;
        std::replace(optionPath.begin(), optionPath.end(), '.', ':');
        const bool known = Config::mgr()->getConfigValue(optionPath).dataptr != nullptr;
        const bool table = lua_istable(L, -1);

        if (!known) {
            if (table) {
                lua_newtable(L);
                const int subDst = lua_gettop(L);
                copyValidatedTable(ctx, subDst - 1, subDst, dotted, depth + 1);
                lua_setfield(L, dstIdx, key.c_str());
            } else {
                ctx.report(std::format("unknown option '{}'", dotted));
            }
        } else if (table) {
            ctx.report(std::format("option '{}' expects a value, not a table", dotted));
        } else {
            lua_pushvalue(L, -1);
            lua_setfield(L, dstIdx, key.c_str());
            ctx.applied.emplace_back(std::move(optionPath), dotted);
        }
        lua_pop(L, 1);
    }
}

// Stack on entry: [T, ..., hl, config]. Returns true with [T, ..., hl], or false with the error message on top.
static bool forwardLuaConfig(lua_State* L) {
    SLuaConfigContext ctx{.L = L, .luaMgr = luaConfigManager(), .where = callerLocation(L), .applied = {}};

    lua_newtable(L); // root
    lua_newtable(L); // plugin
    lua_newtable(L); // hyprglass
    copyValidatedTable(ctx, 1, lua_gettop(L), "");
    lua_setfield(L, -2, "hyprglass");
    lua_setfield(L, -2, "plugin");

    const std::string errorsBefore = ctx.luaMgr.getErrors();
    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
        return false;

    // hl.config reports type and range errors itself; only look for silent no-ops while the config file is parsed.
    if (!ctx.luaMgr.isDynamicParse() && ctx.luaMgr.getErrors() == errorsBefore) {
        for (const auto& [optionPath, dotted] : ctx.applied) {
            if (!Config::mgr()->getConfigValue(optionPath).setByUser)
                ctx.report(std::format("option '{}' was not applied", dotted));
        }
    }
    return true;
}

static int handleLuaConfig(lua_State* L) {
    s_hyprglassCalledThisReload = true;

    if (lua_gettop(L) < 1 || !lua_istable(L, 1))
        return luaL_error(L, "hyprglass.config: expected a table");

    lua_getglobal(L, "hl");
    if (!lua_istable(L, -1))
        return luaL_error(L, "hyprglass.config: hl.config is not available");
    lua_getfield(L, -1, "config");
    if (!lua_isfunction(L, -1))
        return luaL_error(L, "hyprglass.config: hl.config is not available");

    if (!forwardLuaConfig(L))
        return lua_error(L);
    lua_pop(L, 1); // hl
    return 0;
}

// ── Lua preset handler ──────────────────────────────────────────────────────

static std::optional<double> luaFiniteNumber(lua_State* L, int index) {
    if (lua_isnumber(L, index)) {
        const double value = lua_tonumber(L, index);
        if (std::isfinite(value))
            return value;
    }
    return std::nullopt;
}

// Booleans read as 0/1, so a flag like xray takes `true` as hyprglass.config() does.
static std::optional<double> luaPresetValue(lua_State* L, int index) {
    if (lua_isboolean(L, index))
        return lua_toboolean(L, index) ? 1.0 : 0.0;
    return luaFiniteNumber(L, index);
}

// Stack: the key (a string) at -2 and its value at -1. scope names the table in the messages.
static void readPresetField(lua_State* L, SPresetValues& values, const std::string& scope) {
    const std::string_view key = lua_tostring(L, -2);

    float*   floatField = presetFloatField(values, key);
    int64_t* intField   = floatField ? nullptr : presetIntField(values, key);
    if (!floatField && !intField) {
        reportLuaError(L, "preset", std::format("unknown setting '{}' in '{}'", key, scope));
        return;
    }

    const auto value = luaPresetValue(L, -1);
    if (!value) {
        reportLuaError(L, "preset", std::format("'{}' in '{}' expects a number or true/false", key, scope));
        return;
    }

    if (floatField)
        *floatField = static_cast<float>(*value);
    else
        *intField = static_cast<int64_t>(std::clamp(*value, -9.0e18, 9.0e18)); // keeps the cast defined
}

static void readPresetValuesFromTable(lua_State* L, int tableIdx, SPresetValues& values, const std::string& scope) {
    lua_pushnil(L);
    while (lua_next(L, tableIdx) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING)
            readPresetField(L, values, scope);
        else
            reportLuaError(L, "preset", std::format("ignoring non-string key in '{}'", scope));
        lua_pop(L, 1);
    }
}

// preset("clear", { glass_opacity = 0.8, dark = { brightness = 0.7 }, ... })
static int handleLuaPreset(lua_State* L) {
    s_hyprglassCalledThisReload = true;

    // reported, not raised: a Lua error here would abort the rest of the user's config
    if (lua_gettop(L) != 2 || !lua_isstring(L, 1) || !lua_istable(L, 2)) {
        reportLuaError(L, "preset", "expected a name and a table of settings: hg.preset(\"name\", { glass_opacity = 0.8 })");
        return 0;
    }

    const std::string baseName = stripDynamicTagMarker(lua_tostring(L, 1));
    auto&             preset   = s_pendingPresets[baseName];
    preset.name                = baseName;

    lua_pushnil(L);
    while (lua_next(L, 2) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            reportLuaError(L, "preset", std::format("ignoring non-string key in '{}'", baseName));
        } else {
            const std::string_view key = lua_tostring(L, -2);

            if (key == "inherits") {
                if (lua_type(L, -1) == LUA_TSTRING)
                    preset.inherits = stripDynamicTagMarker(lua_tostring(L, -1));
                else
                    reportLuaError(L, "preset", std::format("'inherits' in '{}' expects a preset name", baseName));
            } else if (key == "dark" || key == "light") {
                if (lua_istable(L, -1))
                    readPresetValuesFromTable(L, lua_gettop(L), key == "dark" ? preset.dark : preset.light, std::format("{}.{}", baseName, key));
                else
                    reportLuaError(L, "preset", std::format("'{}' in '{}' expects a table", key, baseName));
            } else {
                readPresetField(L, preset.shared, baseName);
            }
        }
        lua_pop(L, 1);
    }
    return 0;
}

// ── Lua layer handler ───────────────────────────────────────────────────────

struct SPendingLayer {
    std::string                   ns;
    std::string                   preset;
    float                         maskThreshold = -1.0f;
    float                         maskFeather   = -1.0f;
    bool                          exclude       = false;
    int                           liveResample  = -1; // -1 = not set
    std::optional<ELayerMaskMode> maskMode;
    std::optional<SLayerShape>    shape;
};

static std::vector<SPendingLayer> s_pendingLayers;

// shape = { inset = px or {top, right, bottom, left}, radius = px }, both optional (0).
static std::optional<SLayerShape> readLuaLayerShape(lua_State* L, int tableIdx) {
    SLayerShape shape;
    bool        valid = true;

    lua_getfield(L, tableIdx, "inset");
    if (lua_isnumber(L, -1)) {
        const double inset = lua_tonumber(L, -1);
        valid = valid && std::isfinite(inset);
        shape.insetTop = shape.insetRight = shape.insetBottom = shape.insetLeft = clampShapeInset(inset);
    } else if (lua_istable(L, -1)) {
        const int                   insetIdx = lua_gettop(L);
        const std::array<float*, 4> sides    = {&shape.insetTop, &shape.insetRight, &shape.insetBottom, &shape.insetLeft};
        for (size_t i = 0; i < sides.size(); i++) {
            lua_rawgeti(L, insetIdx, static_cast<lua_Integer>(i + 1));
            const double side = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : std::nan("");
            valid             = valid && std::isfinite(side);
            *sides[i]         = clampShapeInset(side);
            lua_pop(L, 1);
        }
    } else if (!lua_isnil(L, -1)) {
        valid = false;
    }
    lua_pop(L, 1);

    lua_getfield(L, tableIdx, "radius");
    if (lua_isnumber(L, -1)) {
        const double radius = lua_tonumber(L, -1);
        valid               = valid && std::isfinite(radius);
        shape.radius        = clampShapeRadius(radius);
    } else if (!lua_isnil(L, -1)) {
        valid = false;
    }
    lua_pop(L, 1);

    if (!valid)
        return std::nullopt;
    return shape;
}

// What hyprglass.layer takes in its table; anything else is reported.
static constexpr std::array<std::string_view, 7> LAYER_SETTINGS = {"exclude", "preset", "mask_threshold", "mask_feather", "live_resample", "mask_mode", "shape"};

static int handleLuaLayer(lua_State* L) {
    s_hyprglassCalledThisReload = true;

    if (lua_gettop(L) < 1 || !lua_isstring(L, 1)) {
        reportLuaError(L, "layer", "first argument must be a namespace string: hg.layer(\"waybar\", { preset = \"subtle\" })");
        return 0;
    }

    SPendingLayer entry;
    entry.ns = lua_tostring(L, 1);

    // from here on problems go to reportLuaError: a Lua error would unwind past entry's strings
    const auto typeError = [L, &entry](std::string_view setting, std::string_view expected) {
        reportLuaError(L, "layer", std::format("'{}' for '{}' expects {}", setting, entry.ns, expected));
    };

    const bool hasTable = lua_gettop(L) >= 2 && lua_istable(L, 2);
    if (lua_gettop(L) >= 2 && !lua_isnil(L, 2) && !hasTable)
        reportLuaError(L, "layer", std::format("settings for '{}' must be a table", entry.ns));

    if (hasTable) {
        lua_pushnil(L);
        while (lua_next(L, 2) != 0) {
            if (entry.ns == LAYER_NAMESPACE_WILDCARD) {
                lua_pop(L, 2);
                reportLuaError(L, "layer", "'*' takes no settings, put the settings for every layer in the layers table of hyprglass.config");
                return 0;
            }
            if (lua_type(L, -2) != LUA_TSTRING)
                reportLuaError(L, "layer", std::format("ignoring non-string key for '{}'", entry.ns));
            else if (std::ranges::find(LAYER_SETTINGS, std::string_view(lua_tostring(L, -2))) == LAYER_SETTINGS.end())
                reportLuaError(L, "layer", std::format("unknown setting '{}' for '{}'", lua_tostring(L, -2), entry.ns));
            lua_pop(L, 1);
        }

        lua_getfield(L, 2, "exclude");
        if (lua_isboolean(L, -1)) {
            entry.exclude = lua_toboolean(L, -1);
        } else if (!lua_isnil(L, -1)) {
            // keeping the entry would turn the exclusion into a whitelist entry
            typeError("exclude", "true or false");
            return 0;
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "preset");
        if (lua_type(L, -1) == LUA_TSTRING)
            entry.preset = lua_tostring(L, -1);
        else if (!lua_isnil(L, -1))
            typeError("preset", "a preset name");
        lua_pop(L, 1);

        lua_getfield(L, 2, "mask_threshold");
        if (const auto value = luaFiniteNumber(L, -1); value && *value >= 0.0)
            entry.maskThreshold = static_cast<float>(*value);
        else if (!lua_isnil(L, -1))
            typeError("mask_threshold", "a finite number, 0 or more");
        lua_pop(L, 1);

        lua_getfield(L, 2, "mask_feather");
        if (const auto value = luaFiniteNumber(L, -1); value && *value >= 0.0)
            entry.maskFeather = static_cast<float>(*value);
        else if (!lua_isnil(L, -1))
            typeError("mask_feather", "a finite number, 0 or more");
        lua_pop(L, 1);

        lua_getfield(L, 2, "live_resample");
        if (lua_isboolean(L, -1))
            entry.liveResample = lua_toboolean(L, -1) ? 1 : 0;
        else if (!lua_isnil(L, -1))
            typeError("live_resample", "true or false");
        lua_pop(L, 1);

        lua_getfield(L, 2, "mask_mode");
        if (lua_type(L, -1) == LUA_TSTRING) {
            entry.maskMode = parseLayerMaskMode(lua_tostring(L, -1));
            if (!entry.maskMode)
                typeError("mask_mode", "\"auto\", \"alpha\", \"region\" or \"contour\"");
        } else if (!lua_isnil(L, -1)) {
            typeError("mask_mode", "\"auto\", \"alpha\", \"region\" or \"contour\"");
        }
        lua_pop(L, 1);

        lua_getfield(L, 2, "shape");
        if (!lua_isnil(L, -1)) {
            entry.shape = lua_istable(L, -1) ? readLuaLayerShape(L, lua_gettop(L)) : std::nullopt;
            if (!entry.shape)
                reportLuaError(L, "layer", std::format("invalid shape for '{}', expected {{ inset = px or {{top, right, bottom, left}}, radius = px }}", entry.ns));
        }
        lua_pop(L, 1);
    }

    s_pendingLayers.push_back(std::move(entry));
    return 0;
}

static void clearPendingLayers() {
    s_pendingLayers.clear();
}

void beginConfigReload() {
    clearPendingPresets();
    clearPendingLayers();
    s_hyprglassCalledThisReload = false;
}

// A hg.layer() line removed from the config must stop applying on the next reload.
static void resetLayerNamespaces() {
    g_pGlobalState->layerNamespaceFilter.clear();
    g_pGlobalState->layerNamespaceExclude.clear();
    g_pGlobalState->layerNamespacePresets.clear();
    g_pGlobalState->layerNamespaceMaskThresholds.clear();
    g_pGlobalState->layerNamespaceLiveResample.clear();
    g_pGlobalState->layerNamespaceMaskModes.clear();
    g_pGlobalState->layerNamespaceMaskFeathers.clear();
    g_pGlobalState->layerNamespaceShapes.clear();
}

void commitPendingLayers() {
    if (!g_pGlobalState || keepCommittedState()) return;
    resetLayerNamespaces();
    for (const auto& entry : s_pendingLayers) {
        if (entry.exclude) {
            g_pGlobalState->layerNamespaceExclude.insert(entry.ns);
        } else {
            g_pGlobalState->layerNamespaceFilter.insert(entry.ns);
            if (!entry.preset.empty())
                g_pGlobalState->layerNamespacePresets[entry.ns] = entry.preset;
            if (entry.maskThreshold >= 0.0f)
                g_pGlobalState->layerNamespaceMaskThresholds[entry.ns] = entry.maskThreshold;
            if (entry.maskFeather >= 0.0f)
                g_pGlobalState->layerNamespaceMaskFeathers[entry.ns] = entry.maskFeather;
            if (entry.liveResample >= 0)
                g_pGlobalState->layerNamespaceLiveResample[entry.ns] = entry.liveResample != 0;
            if (entry.maskMode)
                g_pGlobalState->layerNamespaceMaskModes[entry.ns] = *entry.maskMode;
            if (entry.shape)
                g_pGlobalState->layerNamespaceShapes[entry.ns] = *entry.shape;
        }
    }
    s_pendingLayers.clear();
}

void validateConfig() {
    if (!g_pGlobalState) return;

    const auto& config = g_pGlobalState->config;

    const auto theme = readStringConfig(config.defaultTheme);
    if (theme != "dark" && theme != "light") {
        HyprlandAPI::addNotificationV2(PHANDLE, {
            {"text", std::string("[hyprglass] Invalid default_theme '") + std::string(theme) + "', expected 'dark' or 'light'. Falling back to 'dark'."},
            {"time", (uint64_t)5000},
            {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
        });
    }

    const auto maskMode = readStringConfig(config.layersMaskMode);
    if (!parseLayerMaskMode(maskMode)) {
        HyprlandAPI::addNotificationV2(PHANDLE, {
            {"text", std::string("[hyprglass] Invalid layers.mask_mode '") + std::string(maskMode) + "', expected 'auto', 'alpha', 'region' or 'contour'. Falling back to 'auto'."},
            {"time", (uint64_t)5000},
            {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
        });
    }

    const auto debugMode = readStringConfig(config.debugMode);
    if (!parseDebugMode(debugMode)) {
        HyprlandAPI::addNotificationV2(PHANDLE, {
            {"text", std::string("[hyprglass] Invalid debug.mode '") + std::string(debugMode) + "', expected 'off', 'hints_only', or 'gl_work_only'. Falling back to 'off'."},
            {"time", (uint64_t)5000},
            {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
        });
    }

    const auto preset = readStringConfig(config.defaultPreset);
    if (!preset.empty() && preset != "default") {
        const auto& presets = g_pGlobalState->customPresets;
        if (presets.find(std::string(preset)) == presets.end()) {
            HyprlandAPI::addNotificationV2(PHANDLE, {
                {"text", std::string("[hyprglass] Unknown default_preset '") + std::string(preset) + "'. Using 'default' resolution chain."},
                {"time", (uint64_t)5000},
                {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
            });
        }
    }
}

bool anySelfSampleConfigured(const SPluginConfig& config, const std::unordered_map<std::string, SCustomPreset>& customPresets) {
    for (const auto* layer : {&config.global, &config.dark, &config.light}) {
        if (auto ptr = layer->selfSample; ptr && *ptr && **ptr > 0.0)
            return true;
    }

    for (const auto& [_, preset] : customPresets) {
        if (preset.shared.selfSample > 0.0f || preset.dark.selfSample > 0.0f || preset.light.selfSample > 0.0f)
            return true;
    }

    return false;
}

// ── Preset-aware resolution ──────────────────────────────────────────────────

static float resolvePresetFloatImpl(
    const std::string& presetName, bool isDark,
    float SPresetValues::* presetField,
    Config::FLOAT* const* SOverridableConfig::* configField,
    const SPluginConfig& config,
    const std::unordered_map<std::string, SCustomPreset>& customPresets,
    float hardcodedDefault, int depth
) {
    if (depth < MAX_PRESET_INHERITANCE_DEPTH) {
        if (auto it = customPresets.find(presetName); it != customPresets.end()) {
            const auto& preset = it->second;

            const auto& themeVariant = isDark ? preset.dark : preset.light;
            if (themeVariant.*presetField >= 0.0f) return themeVariant.*presetField;

            if (preset.shared.*presetField >= 0.0f) return preset.shared.*presetField;

            if (!preset.inherits.empty())
                return resolvePresetFloatImpl(preset.inherits, isDark, presetField, configField,
                                             config, customPresets, hardcodedDefault, depth + 1);
        }
    }

    // Built-in theme override
    const auto& themeConfig = isDark ? config.dark : config.light;
    if (auto ptr = themeConfig.*configField; ptr && *ptr) {
        const float themeValue = static_cast<float>(**ptr);
        if (themeValue >= 0.0f) return themeValue;
    }

    // Global
    if (auto ptr = config.global.*configField; ptr && *ptr) {
        const float globalValue = static_cast<float>(**ptr);
        if (globalValue >= 0.0f) return globalValue;
    }

    return hardcodedDefault;
}

float resolvePresetFloat(
    const SResolveContext& context,
    float SPresetValues::* presetField,
    Config::FLOAT* const* SOverridableConfig::* configField,
    float hardcodedDefault
) {
    return resolvePresetFloatImpl(context.presetName, context.isDark, presetField, configField,
                                 context.config, context.customPresets, hardcodedDefault, 0);
}

static int64_t resolvePresetIntImpl(
    const std::string& presetName, bool isDark,
    int64_t SPresetValues::* presetField,
    Config::INTEGER* const* SOverridableConfig::* configField,
    const SPluginConfig& config,
    const std::unordered_map<std::string, SCustomPreset>& customPresets,
    int64_t hardcodedDefault, int depth
) {
    if (depth < MAX_PRESET_INHERITANCE_DEPTH) {
        if (auto it = customPresets.find(presetName); it != customPresets.end()) {
            const auto& preset = it->second;

            const auto& themeVariant = isDark ? preset.dark : preset.light;
            if (themeVariant.*presetField >= 0) return themeVariant.*presetField;

            if (preset.shared.*presetField >= 0) return preset.shared.*presetField;

            if (!preset.inherits.empty())
                return resolvePresetIntImpl(preset.inherits, isDark, presetField, configField,
                                           config, customPresets, hardcodedDefault, depth + 1);
        }
    }

    // Built-in theme override
    const auto& themeConfig = isDark ? config.dark : config.light;
    if (auto ptr = themeConfig.*configField; ptr && *ptr) {
        const int64_t themeValue = **ptr;
        if (themeValue >= 0) return themeValue;
    }

    // Global
    if (auto ptr = config.global.*configField; ptr && *ptr) {
        const int64_t globalValue = **ptr;
        if (globalValue >= 0) return globalValue;
    }

    return hardcodedDefault;
}

int64_t resolvePresetInt(
    const SResolveContext& context,
    int64_t SPresetValues::* presetField,
    Config::INTEGER* const* SOverridableConfig::* configField,
    int64_t hardcodedDefault
) {
    return resolvePresetIntImpl(context.presetName, context.isDark, presetField, configField,
                                context.config, context.customPresets, hardcodedDefault, 0);
}
