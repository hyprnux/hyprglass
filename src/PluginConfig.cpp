#include "PluginConfig.hpp"
#include "BuiltInPresets.hpp"
#include "Globals.hpp"

#include <algorithm>
#include <charconv>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>

// ── Config registration ──────────────────────────────────────────────────────

void registerConfig(HANDLE handle) {
    HyprlandAPI::addConfigValue(handle, ConfigKeys::ENABLED, Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DEFAULT_THEME, Hyprlang::STRING{"dark"});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DEFAULT_PRESET, Hyprlang::STRING{"default"});

    // Global level — real defaults for effect settings,
    // sentinel for theme-sensitive settings (fallback to hardcoded theme defaults)
    HyprlandAPI::addConfigValue(handle, ConfigKeys::BLUR_STRENGTH, Hyprlang::FLOAT{GlobalDefaults::BLUR_STRENGTH});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::BLUR_ITERATIONS, Hyprlang::INT{GlobalDefaults::BLUR_ITERATIONS});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::REFRACTION_STRENGTH, Hyprlang::FLOAT{GlobalDefaults::REFRACTION_STRENGTH});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::CHROMATIC_ABERRATION, Hyprlang::FLOAT{GlobalDefaults::CHROMATIC_ABERRATION});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::FRESNEL_STRENGTH, Hyprlang::FLOAT{GlobalDefaults::FRESNEL_STRENGTH});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::SPECULAR_STRENGTH, Hyprlang::FLOAT{GlobalDefaults::SPECULAR_STRENGTH});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::GLASS_OPACITY, Hyprlang::FLOAT{GlobalDefaults::GLASS_OPACITY});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::EDGE_THICKNESS, Hyprlang::FLOAT{GlobalDefaults::EDGE_THICKNESS});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::TINT_COLOR, Hyprlang::INT{GlobalDefaults::TINT_COLOR});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LENS_DISTORTION, Hyprlang::FLOAT{GlobalDefaults::LENS_DISTORTION});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::BRIGHTNESS, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::CONTRAST, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::SATURATION, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::VIBRANCY, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::VIBRANCY_DARKNESS, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::ADAPTIVE_DIM, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::ADAPTIVE_BOOST, SENTINEL_FLOAT);

    // Dark theme overrides — all sentinel (inherit from global)
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_BLUR_STRENGTH, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_BLUR_ITERATIONS, SENTINEL_INT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_REFRACTION_STRENGTH, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_CHROMATIC_ABERRATION, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_FRESNEL_STRENGTH, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_SPECULAR_STRENGTH, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_GLASS_OPACITY, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_EDGE_THICKNESS, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_TINT_COLOR, SENTINEL_INT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_LENS_DISTORTION, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_BRIGHTNESS, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_CONTRAST, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_SATURATION, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_VIBRANCY, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_VIBRANCY_DARKNESS, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_ADAPTIVE_DIM, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_ADAPTIVE_BOOST, SENTINEL_FLOAT);

    // Light theme overrides — all sentinel (inherit from global)
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_BLUR_STRENGTH, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_BLUR_ITERATIONS, SENTINEL_INT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_REFRACTION_STRENGTH, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_CHROMATIC_ABERRATION, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_FRESNEL_STRENGTH, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_SPECULAR_STRENGTH, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_GLASS_OPACITY, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_EDGE_THICKNESS, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_TINT_COLOR, SENTINEL_INT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_LENS_DISTORTION, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_BRIGHTNESS, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_CONTRAST, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_SATURATION, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_VIBRANCY, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_VIBRANCY_DARKNESS, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_ADAPTIVE_DIM, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_ADAPTIVE_BOOST, SENTINEL_FLOAT);

    // Preset keyword — parsed dynamically during config reload
    HyprlandAPI::addConfigKeyword(handle, ConfigKeys::PRESET_KEYWORD, handlePresetKeyword, Hyprlang::SHandlerOptions{});
}

// ── Config pointer initialization ────────────────────────────────────────────

template <typename T>
static auto* getStaticPtr(HANDLE handle, const char* key) {
    return (T* const*)HyprlandAPI::getConfigValue(handle, key)->getDataStaticPtr();
}

static void initOverridablePointers(HANDLE handle, SOverridableConfig& layer,
                                    const char* blurStrength, const char* blurIterations,
                                    const char* refractionStrength, const char* chromaticAberration,
                                    const char* fresnelStrength, const char* specularStrength,
                                    const char* glassOpacity, const char* edgeThickness,
                                    const char* tintColor, const char* lensDistortion,
                                    const char* brightness, const char* contrast,
                                    const char* saturation, const char* vibrancy,
                                    const char* vibrancyDarkness, const char* adaptiveDim,
                                    const char* adaptiveBoost) {
    layer.blurStrength        = getStaticPtr<Hyprlang::FLOAT>(handle, blurStrength);
    layer.blurIterations      = getStaticPtr<Hyprlang::INT>(handle, blurIterations);
    layer.refractionStrength  = getStaticPtr<Hyprlang::FLOAT>(handle, refractionStrength);
    layer.chromaticAberration = getStaticPtr<Hyprlang::FLOAT>(handle, chromaticAberration);
    layer.fresnelStrength     = getStaticPtr<Hyprlang::FLOAT>(handle, fresnelStrength);
    layer.specularStrength    = getStaticPtr<Hyprlang::FLOAT>(handle, specularStrength);
    layer.glassOpacity        = getStaticPtr<Hyprlang::FLOAT>(handle, glassOpacity);
    layer.edgeThickness       = getStaticPtr<Hyprlang::FLOAT>(handle, edgeThickness);
    layer.tintColor           = getStaticPtr<Hyprlang::INT>(handle, tintColor);
    layer.lensDistortion      = getStaticPtr<Hyprlang::FLOAT>(handle, lensDistortion);
    layer.brightness          = getStaticPtr<Hyprlang::FLOAT>(handle, brightness);
    layer.contrast            = getStaticPtr<Hyprlang::FLOAT>(handle, contrast);
    layer.saturation          = getStaticPtr<Hyprlang::FLOAT>(handle, saturation);
    layer.vibrancy            = getStaticPtr<Hyprlang::FLOAT>(handle, vibrancy);
    layer.vibrancyDarkness    = getStaticPtr<Hyprlang::FLOAT>(handle, vibrancyDarkness);
    layer.adaptiveDim         = getStaticPtr<Hyprlang::FLOAT>(handle, adaptiveDim);
    layer.adaptiveBoost       = getStaticPtr<Hyprlang::FLOAT>(handle, adaptiveBoost);
}

void initConfigPointers(HANDLE handle, SPluginConfig& config) {
    config.enabled       = getStaticPtr<Hyprlang::INT>(handle, ConfigKeys::ENABLED);
    config.defaultTheme  = (Hyprlang::STRING const*)HyprlandAPI::getConfigValue(handle, ConfigKeys::DEFAULT_THEME)->getDataStaticPtr();
    config.defaultPreset = (Hyprlang::STRING const*)HyprlandAPI::getConfigValue(handle, ConfigKeys::DEFAULT_PRESET)->getDataStaticPtr();

    initOverridablePointers(handle, config.global,
        ConfigKeys::BLUR_STRENGTH, ConfigKeys::BLUR_ITERATIONS,
        ConfigKeys::REFRACTION_STRENGTH, ConfigKeys::CHROMATIC_ABERRATION,
        ConfigKeys::FRESNEL_STRENGTH, ConfigKeys::SPECULAR_STRENGTH,
        ConfigKeys::GLASS_OPACITY, ConfigKeys::EDGE_THICKNESS,
        ConfigKeys::TINT_COLOR, ConfigKeys::LENS_DISTORTION,
        ConfigKeys::BRIGHTNESS, ConfigKeys::CONTRAST,
        ConfigKeys::SATURATION, ConfigKeys::VIBRANCY,
        ConfigKeys::VIBRANCY_DARKNESS, ConfigKeys::ADAPTIVE_DIM,
        ConfigKeys::ADAPTIVE_BOOST);

    initOverridablePointers(handle, config.dark,
        ConfigKeys::DARK_BLUR_STRENGTH, ConfigKeys::DARK_BLUR_ITERATIONS,
        ConfigKeys::DARK_REFRACTION_STRENGTH, ConfigKeys::DARK_CHROMATIC_ABERRATION,
        ConfigKeys::DARK_FRESNEL_STRENGTH, ConfigKeys::DARK_SPECULAR_STRENGTH,
        ConfigKeys::DARK_GLASS_OPACITY, ConfigKeys::DARK_EDGE_THICKNESS,
        ConfigKeys::DARK_TINT_COLOR, ConfigKeys::DARK_LENS_DISTORTION,
        ConfigKeys::DARK_BRIGHTNESS, ConfigKeys::DARK_CONTRAST,
        ConfigKeys::DARK_SATURATION, ConfigKeys::DARK_VIBRANCY,
        ConfigKeys::DARK_VIBRANCY_DARKNESS, ConfigKeys::DARK_ADAPTIVE_DIM,
        ConfigKeys::DARK_ADAPTIVE_BOOST);

    initOverridablePointers(handle, config.light,
        ConfigKeys::LIGHT_BLUR_STRENGTH, ConfigKeys::LIGHT_BLUR_ITERATIONS,
        ConfigKeys::LIGHT_REFRACTION_STRENGTH, ConfigKeys::LIGHT_CHROMATIC_ABERRATION,
        ConfigKeys::LIGHT_FRESNEL_STRENGTH, ConfigKeys::LIGHT_SPECULAR_STRENGTH,
        ConfigKeys::LIGHT_GLASS_OPACITY, ConfigKeys::LIGHT_EDGE_THICKNESS,
        ConfigKeys::LIGHT_TINT_COLOR, ConfigKeys::LIGHT_LENS_DISTORTION,
        ConfigKeys::LIGHT_BRIGHTNESS, ConfigKeys::LIGHT_CONTRAST,
        ConfigKeys::LIGHT_SATURATION, ConfigKeys::LIGHT_VIBRANCY,
        ConfigKeys::LIGHT_VIBRANCY_DARKNESS, ConfigKeys::LIGHT_ADAPTIVE_DIM,
        ConfigKeys::LIGHT_ADAPTIVE_BOOST);
}

// ── Preset keyword parsing ───────────────────────────────────────────────────

// Presets built during config parse, swapped into g_pGlobalState on configReloaded
static std::unordered_map<std::string, SCustomPreset> s_pendingPresets;

static std::string_view trim(std::string_view str) {
    while (!str.empty() && std::isspace(static_cast<unsigned char>(str.front()))) str.remove_prefix(1);
    while (!str.empty() && std::isspace(static_cast<unsigned char>(str.back())))  str.remove_suffix(1);
    return str;
}

static bool setPresetFloatField(SPresetValues& values, std::string_view key, std::string_view valueStr) {
    float parsed = 0.0f;
    auto [ptr, ec] = std::from_chars(valueStr.data(), valueStr.data() + valueStr.size(), parsed);
    if (ec != std::errc{}) return false;

    if (key == "blur_strength")        { values.blurStrength = parsed; return true; }
    if (key == "refraction_strength")  { values.refractionStrength = parsed; return true; }
    if (key == "chromatic_aberration") { values.chromaticAberration = parsed; return true; }
    if (key == "fresnel_strength")     { values.fresnelStrength = parsed; return true; }
    if (key == "specular_strength")    { values.specularStrength = parsed; return true; }
    if (key == "glass_opacity")        { values.glassOpacity = parsed; return true; }
    if (key == "edge_thickness")       { values.edgeThickness = parsed; return true; }
    if (key == "lens_distortion")      { values.lensDistortion = parsed; return true; }
    if (key == "brightness")           { values.brightness = parsed; return true; }
    if (key == "contrast")             { values.contrast = parsed; return true; }
    if (key == "saturation")           { values.saturation = parsed; return true; }
    if (key == "vibrancy")             { values.vibrancy = parsed; return true; }
    if (key == "vibrancy_darkness")    { values.vibrancyDarkness = parsed; return true; }
    if (key == "adaptive_dim")         { values.adaptiveDim = parsed; return true; }
    if (key == "adaptive_boost")       { values.adaptiveBoost = parsed; return true; }
    return false;
}

static bool setPresetIntField(SPresetValues& values, std::string_view key, std::string_view valueStr) {
    // Handle hex (0x...) and decimal
    int64_t parsed = 0;
    int base = 10;
    auto data = valueStr.data();
    auto size = valueStr.size();
    if (size > 2 && data[0] == '0' && (data[1] == 'x' || data[1] == 'X')) {
        data += 2;
        size -= 2;
        base = 16;
    }
    auto [ptr, ec] = std::from_chars(data, data + size, parsed, base);
    if (ec != std::errc{}) return false;

    if (key == "blur_iterations") { values.blurIterations = parsed; return true; }
    if (key == "tint_color")      { values.tintColor = parsed; return true; }
    return false;
}

static bool setPresetField(SPresetValues& values, std::string_view key, std::string_view valueStr) {
    return setPresetIntField(values, key, valueStr) || setPresetFloatField(values, key, valueStr);
}

Hyprlang::CParseResult handlePresetKeyword(const char* /*command*/, const char* value) {
    Hyprlang::CParseResult result;
    std::string_view       input(value);

    std::string presetName;
    std::string variant;     // "", "dark", or "light"
    std::string inherits;
    SPresetValues parsedValues;

    // Split on ',' and parse key:value tokens
    while (!input.empty()) {
        auto commaPos = input.find(',');
        auto token = trim(input.substr(0, commaPos));
        input = (commaPos == std::string_view::npos) ? std::string_view{} : input.substr(commaPos + 1);

        if (token.empty()) continue;

        auto colonPos = token.find(':');
        if (colonPos == std::string_view::npos) {
            result.setError(std::format("preset: invalid token '{}' (expected key:value)", token).c_str());
            return result;
        }

        auto key = trim(token.substr(0, colonPos));
        auto val = trim(token.substr(colonPos + 1));

        if (key == "name") {
            // val is "presetname" or "presetname:dark" or "presetname:light"
            auto variantSep = val.find(':');
            if (variantSep != std::string_view::npos) {
                presetName = std::string(val.substr(0, variantSep));
                variant = std::string(val.substr(variantSep + 1));
                if (variant != "dark" && variant != "light") {
                    result.setError(std::format("preset: invalid variant '{}' (expected dark or light)", variant).c_str());
                    return result;
                }
            } else {
                presetName = std::string(val);
            }
        } else if (key == "inherits") {
            inherits = std::string(val);
        } else {
            if (!setPresetField(parsedValues, key, val)) {
                result.setError(std::format("preset: unknown or invalid setting '{}:{}'", key, val).c_str());
                return result;
            }
        }
    }

    if (presetName.empty()) {
        result.setError("preset: missing required 'name' field");
        return result;
    }

    // Get or create the preset entry
    auto& preset = s_pendingPresets[presetName];
    preset.name = presetName;

    if (!inherits.empty())
        preset.inherits = inherits;

    // Assign parsed values to the correct layer
    if (variant == "dark")
        preset.dark = parsedValues;
    else if (variant == "light")
        preset.light = parsedValues;
    else
        preset.shared = parsedValues;

    return result;
}

void clearPendingPresets() {
    s_pendingPresets.clear();
}

void commitPendingPresets() {
    if (!g_pGlobalState) return;

    // Start with built-in presets, then overlay user-defined ones (user wins)
    auto merged = BuiltInPresets::getAll();
    for (auto& [name, preset] : s_pendingPresets)
        merged[name] = std::move(preset);

    g_pGlobalState->customPresets = std::move(merged);
    s_pendingPresets.clear();
}

void validateConfig() {
    if (!g_pGlobalState) return;

    const auto& config = g_pGlobalState->config;

    if (config.defaultTheme) {
        const char* theme = *config.defaultTheme;
        if (!theme || (std::string_view(theme) != "dark" && std::string_view(theme) != "light")) {
            HyprlandAPI::addNotificationV2(PHANDLE, {
                {"text", std::string("[hyprglass] Invalid default_theme '") + (theme ? theme : "(null)") + "', expected 'dark' or 'light'. Falling back to 'dark'."},
                {"time", (uint64_t)5000},
                {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
            });
        }
    }

    if (config.defaultPreset) {
        const char* preset = *config.defaultPreset;
        if (preset && preset[0] != '\0' && std::string_view(preset) != "default") {
            const auto& presets = g_pGlobalState->customPresets;
            if (presets.find(preset) == presets.end()) {
                HyprlandAPI::addNotificationV2(PHANDLE, {
                    {"text", std::string("[hyprglass] Unknown default_preset '") + preset + "'. Using 'default' resolution chain."},
                    {"time", (uint64_t)5000},
                    {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
                });
            }
        }
    }
}

// ── Preset-aware resolution ──────────────────────────────────────────────────

static float resolvePresetFloatImpl(
    const std::string& presetName, bool isDark,
    float SPresetValues::* presetField,
    Hyprlang::FLOAT* const* SOverridableConfig::* configField,
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
    Hyprlang::FLOAT* const* SOverridableConfig::* configField,
    float hardcodedDefault
) {
    return resolvePresetFloatImpl(context.presetName, context.isDark, presetField, configField,
                                 context.config, context.customPresets, hardcodedDefault, 0);
}

static int64_t resolvePresetIntImpl(
    const std::string& presetName, bool isDark,
    int64_t SPresetValues::* presetField,
    Hyprlang::INT* const* SOverridableConfig::* configField,
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
    Hyprlang::INT* const* SOverridableConfig::* configField,
    int64_t hardcodedDefault
) {
    return resolvePresetIntImpl(context.presetName, context.isDark, presetField, configField,
                                context.config, context.customPresets, hardcodedDefault, 0);
}
