#include "LiquidGlassDecoration.hpp"
#include "globals.hpp"
#include "shaders.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <chrono>

static std::string loadShader(const char* fileName) {
    if (SHADERS.contains(fileName)) {
        return SHADERS.at(fileName);
    }
    const std::string message = std::format("[{}] Failed to load shader: {}", PLUGIN_NAME, fileName);
    HyprlandAPI::addNotification(PHANDLE, message, CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
    throw std::runtime_error(message);
}

void initShaderIfNeeded() {
    if (!g_pGlobalState || g_pGlobalState->shaderInitialized)
        return;

    const char* shaderFile = "liquidglass.frag";

    GLuint prog = g_pHyprOpenGL->createProgram(
        g_pHyprOpenGL->m_shaders->TEXVERTSRC,
        loadShader(shaderFile),
        true
    );

    if (prog == 0) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return;
    }

    g_pGlobalState->shader.program = prog;

    g_pGlobalState->shader.uniformLocations[SHADER_PROJ]       = glGetUniformLocation(prog, "proj");
    g_pGlobalState->shader.uniformLocations[SHADER_POS_ATTRIB] = glGetAttribLocation(prog, "pos");
    g_pGlobalState->shader.uniformLocations[SHADER_TEX_ATTRIB] = glGetAttribLocation(prog, "texcoord");
    g_pGlobalState->shader.uniformLocations[SHADER_TEX]        = glGetUniformLocation(prog, "tex");
    g_pGlobalState->shader.uniformLocations[SHADER_TOP_LEFT]   = glGetUniformLocation(prog, "topLeft");
    g_pGlobalState->shader.uniformLocations[SHADER_FULL_SIZE]  = glGetUniformLocation(prog, "fullSize");
    g_pGlobalState->shader.uniformLocations[SHADER_RADIUS]     = glGetUniformLocation(prog, "radius");

    g_pGlobalState->locTime                  = glGetUniformLocation(prog, "time");
    g_pGlobalState->locBlurStrength          = glGetUniformLocation(prog, "blurStrength");
    g_pGlobalState->locRefractionStrength    = glGetUniformLocation(prog, "refractionStrength");
    g_pGlobalState->locChromaticAberration   = glGetUniformLocation(prog, "chromaticAberration");
    g_pGlobalState->locFresnelStrength       = glGetUniformLocation(prog, "fresnelStrength");
    g_pGlobalState->locSpecularStrength      = glGetUniformLocation(prog, "specularStrength");
    g_pGlobalState->locGlassOpacity          = glGetUniformLocation(prog, "glassOpacity");
    g_pGlobalState->locEdgeThickness         = glGetUniformLocation(prog, "edgeThickness");
    g_pGlobalState->locFullSizeUntransformed = glGetUniformLocation(prog, "fullSizeUntransformed");
    g_pGlobalState->locUvPadding             = glGetUniformLocation(prog, "uvPadding");

    g_pGlobalState->shader.createVao();

    auto now = std::chrono::steady_clock::now();
    g_pGlobalState->startTime = std::chrono::duration<float>(now.time_since_epoch()).count();

    g_pGlobalState->shaderInitialized = true;

    HyprlandAPI::addNotification(PHANDLE,
        std::format("[{}] Shader initialized", PLUGIN_NAME),
        CHyprColor{0.2, 0.8, 0.2, 1.0}, 3000);
}

static void onNewWindow(void* self, std::any data) {
    const auto PWINDOW = std::any_cast<PHLWINDOW>(data);

    if (std::ranges::any_of(PWINDOW->m_windowDecorations,
                            [](const auto& d) { return d->getDisplayName() == "LiquidGlass"; }))
        return;

    auto deco = makeUnique<CLiquidGlassDecoration>(PWINDOW);
    g_pGlobalState->decorations.emplace_back(deco);
    deco->m_self = deco;
    HyprlandAPI::addWindowDecoration(PHANDLE, PWINDOW, std::move(deco));
}

static void onCloseWindow(void* self, std::any data) {
    const auto PWINDOW = std::any_cast<PHLWINDOW>(data);

    std::erase_if(g_pGlobalState->decorations, [PWINDOW](const auto& deco) {
        auto locked = deco.lock();
        return !locked || locked->getOwner() == PWINDOW;
    });
}

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Version mismatch!", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        throw std::runtime_error("Version mismatch");
    }

    g_pGlobalState = std::make_unique<SGlobalState>();

    static auto P1 = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "openWindow",
        [&](void* self, SCallbackInfo& info, std::any data) { onNewWindow(self, data); });

    static auto P2 = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "closeWindow",
        [&](void* self, SCallbackInfo& info, std::any data) { onCloseWindow(self, data); });

    HyprlandAPI::addConfigValue(PHANDLE, "plugin:liquid-glass:enabled", Hyprlang::INT{1});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:liquid-glass:blur_strength", Hyprlang::FLOAT{1.0});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:liquid-glass:refraction_strength", Hyprlang::FLOAT{0.5});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:liquid-glass:chromatic_aberration", Hyprlang::FLOAT{0.3});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:liquid-glass:fresnel_strength", Hyprlang::FLOAT{0.4});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:liquid-glass:specular_strength", Hyprlang::FLOAT{0.4});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:liquid-glass:glass_opacity", Hyprlang::FLOAT{0.85});
    HyprlandAPI::addConfigValue(PHANDLE, "plugin:liquid-glass:edge_thickness", Hyprlang::FLOAT{0.03});

    for (auto& w : g_pCompositor->m_windows) {
        if (w->isHidden() || !w->m_isMapped)
            continue;
        onNewWindow(nullptr, std::any(w));
    }

    HyprlandAPI::reloadConfig();

    HyprlandAPI::addNotification(PHANDLE,
        std::format("[{}] Loaded successfully!", PLUGIN_NAME),
        CHyprColor{0.2, 0.8, 0.4, 1.0}, 4000);

    return {PLUGIN_NAME, PLUGIN_DESCRIPTION, PLUGIN_AUTHOR, PLUGIN_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
    for (auto& deco : g_pGlobalState->decorations) {
        auto locked = deco.lock();
        if (locked) {
            auto owner = locked->getOwner();
            if (owner)
                owner->removeWindowDeco(locked.get());
        }
    }

    g_pHyprRenderer->m_renderPass.removeAllOfType("CLiquidGlassPassElement");

    g_pGlobalState->shader.destroy();
    g_pGlobalState.reset();
}
