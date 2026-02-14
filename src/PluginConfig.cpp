#include "PluginConfig.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>

void registerConfig(HANDLE handle) {
    // Global-only
    HyprlandAPI::addConfigValue(handle, ConfigKeys::ENABLED, Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DEFAULT_THEME, Hyprlang::INT{0});

    // Global level — real defaults for previously global-only settings,
    // sentinel for previously theme-only settings (fallback to hardcoded theme defaults)
    HyprlandAPI::addConfigValue(handle, ConfigKeys::BLUR_STRENGTH, Hyprlang::FLOAT{2.0});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::BLUR_ITERATIONS, Hyprlang::INT{3});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::REFRACTION_STRENGTH, Hyprlang::FLOAT{0.6});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::CHROMATIC_ABERRATION, Hyprlang::FLOAT{0.5});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::FRESNEL_STRENGTH, Hyprlang::FLOAT{0.6});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::SPECULAR_STRENGTH, Hyprlang::FLOAT{0.8});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::GLASS_OPACITY, Hyprlang::FLOAT{1.0});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::EDGE_THICKNESS, Hyprlang::FLOAT{0.06});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::TINT_COLOR, Hyprlang::INT{0x8899aa22});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LENS_DISTORTION, Hyprlang::FLOAT{0.5});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::BRIGHTNESS, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::CONTRAST, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::SATURATION, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::VIBRANCY, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::VIBRANCY_DARKNESS, SENTINEL_FLOAT);
    HyprlandAPI::addConfigValue(handle, ConfigKeys::ADAPTIVE_CORRECTION, SENTINEL_FLOAT);

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
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_ADAPTIVE_CORRECTION, SENTINEL_FLOAT);

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
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_ADAPTIVE_CORRECTION, SENTINEL_FLOAT);
}

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
                                    const char* vibrancyDarkness, const char* adaptiveCorrection) {
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
    layer.adaptiveCorrection  = getStaticPtr<Hyprlang::FLOAT>(handle, adaptiveCorrection);
}

void initConfigPointers(HANDLE handle, SPluginConfig& config) {
    config.enabled      = getStaticPtr<Hyprlang::INT>(handle, ConfigKeys::ENABLED);
    config.defaultTheme = getStaticPtr<Hyprlang::INT>(handle, ConfigKeys::DEFAULT_THEME);

    initOverridablePointers(handle, config.global,
        ConfigKeys::BLUR_STRENGTH, ConfigKeys::BLUR_ITERATIONS,
        ConfigKeys::REFRACTION_STRENGTH, ConfigKeys::CHROMATIC_ABERRATION,
        ConfigKeys::FRESNEL_STRENGTH, ConfigKeys::SPECULAR_STRENGTH,
        ConfigKeys::GLASS_OPACITY, ConfigKeys::EDGE_THICKNESS,
        ConfigKeys::TINT_COLOR, ConfigKeys::LENS_DISTORTION,
        ConfigKeys::BRIGHTNESS, ConfigKeys::CONTRAST,
        ConfigKeys::SATURATION, ConfigKeys::VIBRANCY,
        ConfigKeys::VIBRANCY_DARKNESS, ConfigKeys::ADAPTIVE_CORRECTION);

    initOverridablePointers(handle, config.dark,
        ConfigKeys::DARK_BLUR_STRENGTH, ConfigKeys::DARK_BLUR_ITERATIONS,
        ConfigKeys::DARK_REFRACTION_STRENGTH, ConfigKeys::DARK_CHROMATIC_ABERRATION,
        ConfigKeys::DARK_FRESNEL_STRENGTH, ConfigKeys::DARK_SPECULAR_STRENGTH,
        ConfigKeys::DARK_GLASS_OPACITY, ConfigKeys::DARK_EDGE_THICKNESS,
        ConfigKeys::DARK_TINT_COLOR, ConfigKeys::DARK_LENS_DISTORTION,
        ConfigKeys::DARK_BRIGHTNESS, ConfigKeys::DARK_CONTRAST,
        ConfigKeys::DARK_SATURATION, ConfigKeys::DARK_VIBRANCY,
        ConfigKeys::DARK_VIBRANCY_DARKNESS, ConfigKeys::DARK_ADAPTIVE_CORRECTION);

    initOverridablePointers(handle, config.light,
        ConfigKeys::LIGHT_BLUR_STRENGTH, ConfigKeys::LIGHT_BLUR_ITERATIONS,
        ConfigKeys::LIGHT_REFRACTION_STRENGTH, ConfigKeys::LIGHT_CHROMATIC_ABERRATION,
        ConfigKeys::LIGHT_FRESNEL_STRENGTH, ConfigKeys::LIGHT_SPECULAR_STRENGTH,
        ConfigKeys::LIGHT_GLASS_OPACITY, ConfigKeys::LIGHT_EDGE_THICKNESS,
        ConfigKeys::LIGHT_TINT_COLOR, ConfigKeys::LIGHT_LENS_DISTORTION,
        ConfigKeys::LIGHT_BRIGHTNESS, ConfigKeys::LIGHT_CONTRAST,
        ConfigKeys::LIGHT_SATURATION, ConfigKeys::LIGHT_VIBRANCY,
        ConfigKeys::LIGHT_VIBRANCY_DARKNESS, ConfigKeys::LIGHT_ADAPTIVE_CORRECTION);
}
