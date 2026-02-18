#include "GlassDecoration.hpp"
#include "Globals.hpp"
#include "PluginConfig.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/config/ConfigManager.hpp>

static void onNewWindow(void* self, std::any data) {
    const auto window = std::any_cast<PHLWINDOW>(data);

    if (std::ranges::any_of(window->m_windowDecorations,
                            [](const auto& decoration) { return decoration->getDisplayName() == "HyprGlass"; }))
        return;

    auto decoration = makeUnique<CGlassDecoration>(window);
    g_pGlobalState->decorations.emplace_back(decoration);
    decoration->m_self = decoration;
    HyprlandAPI::addWindowDecoration(PHANDLE, window, std::move(decoration));
}

static void onCloseWindow(void* self, std::any data) {
    const auto window = std::any_cast<PHLWINDOW>(data);

    std::erase_if(g_pGlobalState->decorations, [&window](const auto& decoration) {
        auto locked = decoration.lock();
        return !locked || locked->getOwner() == window;
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

    static auto onOpen = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "openWindow",
        [&](void* self, SCallbackInfo& info, std::any data) { onNewWindow(self, data); });

    static auto onClose = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "closeWindow",
        [&](void* self, SCallbackInfo& info, std::any data) { onCloseWindow(self, data); });

    // Clear pending presets before config re-parse, commit after
    static auto onPreConfigReload = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "preConfigReload",
        [&](void* /*self*/, SCallbackInfo& /*info*/, std::any /*data*/) { clearPendingPresets(); });

    static auto onConfigReloaded = HyprlandAPI::registerCallbackDynamic(
        PHANDLE, "configReloaded",
        [&](void* /*self*/, SCallbackInfo& /*info*/, std::any /*data*/) { commitPendingPresets(); validateConfig(); });

    registerConfig(PHANDLE);
    initConfigPointers(PHANDLE, g_pGlobalState->config);

    // Shadows must be enabled for the glass effect to sample the correct background.
    // Force-enable if the user has disabled them.
    static auto* const PSHADOWENABLED = (Hyprlang::INT* const*)g_pConfigManager->getConfigValuePtr("decoration:shadow:enabled");
    if (PSHADOWENABLED && !**PSHADOWENABLED) {
        HyprlandAPI::invokeHyprctlCommand("keyword", "decoration:shadow:enabled true");
    }

    for (auto& window : g_pCompositor->m_windows) {
        if (window->isHidden() || !window->m_isMapped)
            continue;
        onNewWindow(nullptr, std::any(window));
    }

    HyprlandAPI::reloadConfig();
    validateConfig();

    return {std::string(PLUGIN_NAME), std::string(PLUGIN_DESCRIPTION), std::string(PLUGIN_AUTHOR), std::string(PLUGIN_VERSION)};
}

APICALL EXPORT void PLUGIN_EXIT() {
    for (auto& decoration : g_pGlobalState->decorations) {
        auto locked = decoration.lock();
        if (locked) {
            auto owner = locked->getOwner();
            if (owner)
                owner->removeWindowDeco(locked.get());
        }
    }

    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassPassElement");

    g_pGlobalState->shaderManager.destroy();
    g_pGlobalState.reset();
}
