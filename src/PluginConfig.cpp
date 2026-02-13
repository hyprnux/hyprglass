#include "PluginConfig.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>

void registerConfig(HANDLE handle) {
    HyprlandAPI::addConfigValue(handle, ConfigKeys::ENABLED, Hyprlang::INT{1});
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
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DEFAULT_THEME, Hyprlang::INT{0});

    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_BRIGHTNESS, Hyprlang::FLOAT{0.82});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_CONTRAST, Hyprlang::FLOAT{0.90});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_SATURATION, Hyprlang::FLOAT{0.80});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_VIBRANCY, Hyprlang::FLOAT{0.15});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_VIBRANCY_DARKNESS, Hyprlang::FLOAT{0.0});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::DARK_ADAPTIVE_DIM, Hyprlang::FLOAT{0.4});

    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_BRIGHTNESS, Hyprlang::FLOAT{1.12});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_CONTRAST, Hyprlang::FLOAT{0.92});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_SATURATION, Hyprlang::FLOAT{0.85});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_VIBRANCY, Hyprlang::FLOAT{0.12});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_VIBRANCY_DARKNESS, Hyprlang::FLOAT{0.0});
    HyprlandAPI::addConfigValue(handle, ConfigKeys::LIGHT_ADAPTIVE_BOOST, Hyprlang::FLOAT{0.4});
}

template <typename T>
static auto* getStaticPtr(HANDLE handle, const char* key) {
    return (T* const*)HyprlandAPI::getConfigValue(handle, key)->getDataStaticPtr();
}

void initConfigPointers(HANDLE handle, SPluginConfig& config) {
    config.enabled             = getStaticPtr<Hyprlang::INT>(handle, ConfigKeys::ENABLED);
    config.blurStrength        = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::BLUR_STRENGTH);
    config.blurIterations      = getStaticPtr<Hyprlang::INT>(handle, ConfigKeys::BLUR_ITERATIONS);
    config.refractionStrength  = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::REFRACTION_STRENGTH);
    config.chromaticAberration = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::CHROMATIC_ABERRATION);
    config.fresnelStrength     = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::FRESNEL_STRENGTH);
    config.specularStrength    = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::SPECULAR_STRENGTH);
    config.glassOpacity        = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::GLASS_OPACITY);
    config.edgeThickness       = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::EDGE_THICKNESS);
    config.tintColor           = getStaticPtr<Hyprlang::INT>(handle, ConfigKeys::TINT_COLOR);
    config.lensDistortion      = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::LENS_DISTORTION);
    config.defaultTheme        = getStaticPtr<Hyprlang::INT>(handle, ConfigKeys::DEFAULT_THEME);

    config.darkBrightness       = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::DARK_BRIGHTNESS);
    config.darkContrast         = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::DARK_CONTRAST);
    config.darkSaturation       = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::DARK_SATURATION);
    config.darkVibrancy         = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::DARK_VIBRANCY);
    config.darkVibrancyDarkness = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::DARK_VIBRANCY_DARKNESS);
    config.darkAdaptiveDim      = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::DARK_ADAPTIVE_DIM);

    config.lightBrightness       = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::LIGHT_BRIGHTNESS);
    config.lightContrast         = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::LIGHT_CONTRAST);
    config.lightSaturation       = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::LIGHT_SATURATION);
    config.lightVibrancy         = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::LIGHT_VIBRANCY);
    config.lightVibrancyDarkness = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::LIGHT_VIBRANCY_DARKNESS);
    config.lightAdaptiveBoost    = getStaticPtr<Hyprlang::FLOAT>(handle, ConfigKeys::LIGHT_ADAPTIVE_BOOST);
}
