#include "GlassDecoration.hpp"
#include "GlassLayerCompositeElement.hpp"
#include "GlassLayerPassElement.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "PluginConfig.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/event/EventBus.hpp>

#include <sstream>

static void onNewWindow(PHLWINDOW window) {
    if (std::ranges::any_of(window->m_windowDecorations,
                            [](const auto& decoration) { return decoration->getDisplayName() == "HyprGlass"; }))
        return;

    auto decoration = makeUnique<CGlassDecoration>(window);
    g_pGlobalState->decorations.emplace_back(decoration);
    decoration->m_self = decoration;
    HyprlandAPI::addWindowDecoration(PHANDLE, window, std::move(decoration));
}

static void onCloseWindow(PHLWINDOW window) {
    std::erase_if(g_pGlobalState->decorations, [&window](const auto& decoration) {
        auto locked = decoration.lock();
        return !locked || locked->getOwner() == window;
    });
}

// ── Layer surface support ────────────────────────────────────────────────────

static void parseCommaSeparated(Hyprlang::STRING const* configPtr, std::unordered_set<std::string>& out) {
    out.clear();
    if (!configPtr)
        return;

    const char* raw = *configPtr;
    if (!raw || raw[0] == '\0')
        return;

    std::istringstream stream(raw);
    std::string token;
    while (std::getline(stream, token, ',')) {
        auto start = token.find_first_not_of(" \t");
        auto end   = token.find_last_not_of(" \t");
        if (start != std::string::npos)
            out.insert(token.substr(start, end - start + 1));
    }
}

static void parseNamespacePresets(Hyprlang::STRING const* configPtr, std::unordered_map<std::string, std::string>& out) {
    out.clear();
    if (!configPtr)
        return;

    const char* raw = *configPtr;
    if (!raw || raw[0] == '\0')
        return;

    std::istringstream stream(raw);
    std::string token;
    while (std::getline(stream, token, ',')) {
        auto colonPos = token.find(':');
        if (colonPos == std::string::npos)
            continue;

        auto nsStart = token.find_first_not_of(" \t");
        auto nsEnd   = token.find_last_not_of(" \t", colonPos - 1);
        auto pStart  = token.find_first_not_of(" \t", colonPos + 1);
        auto pEnd    = token.find_last_not_of(" \t");

        if (nsStart != std::string::npos && nsEnd != std::string::npos &&
            pStart != std::string::npos && pEnd != std::string::npos && nsStart <= nsEnd && pStart <= pEnd) {
            out.emplace(token.substr(nsStart, nsEnd - nsStart + 1),
                        token.substr(pStart, pEnd - pStart + 1));
        }
    }
}

static void parseLayerNamespaceFilters() {
    const auto& config = g_pGlobalState->config;
    parseCommaSeparated(config.layersNamespaces, g_pGlobalState->layerNamespaceFilter);
    parseCommaSeparated(config.layersExcludeNamespaces, g_pGlobalState->layerNamespaceExclude);
    parseNamespacePresets(config.layersNamespacePresets, g_pGlobalState->layerNamespacePresets);
}

static bool shouldGlassLayer(PHLLS layerSurface) {
    if (!layerSurface)
        return false;

    const auto& ns = layerSurface->m_namespace;

    // Exclusion takes priority
    if (g_pGlobalState->layerNamespaceExclude.contains(ns))
        return false;

    const auto& include = g_pGlobalState->layerNamespaceFilter;
    if (include.empty())
        return true;

    return include.contains(ns);
}

using renderLayerFn = void (*)(CHyprRenderer*, PHLLS, PHLMONITOR, const Time::steady_tp&, bool, bool);

static void hkRenderLayer(CHyprRenderer* thisptr, PHLLS layerSurface, PHLMONITOR monitor,
                           const Time::steady_tp& now, bool popups, bool lockscreen) {
    const auto& config = g_pGlobalState->config;

    // Only inject glass on the main surface pass, not popups
    if (!popups && config.layersEnabled && **config.layersEnabled && shouldGlassLayer(layerSurface)) {
        // Lazy-create per-layer state, replacing stale entries whose weak ref died
        // (can happen when a new CLayerSurface is allocated at the same address)
        auto* rawPtr = layerSurface.get();
        auto& layerStates = g_pGlobalState->layerSurfaces;
        auto it = layerStates.find(rawPtr);
        if (it != layerStates.end() && !it->second->getLayerSurface()) {
            it->second = std::make_shared<CGlassLayerSurface>(layerSurface);
        } else if (it == layerStates.end()) {
            it = layerStates.emplace(rawPtr, std::make_shared<CGlassLayerSurface>(layerSurface)).first;
        }

        float alpha = layerSurface->m_alpha->value();

        // Pre-surface: sample+blur background, redirect currentFB → temp FBO
        CGlassLayerPassElement::SGlassLayerPassData preData{it->second, alpha};
        g_pHyprRenderer->m_renderPass.add(makeUnique<CGlassLayerPassElement>(preData));

        // Original renderLayer: surface renders into the redirected temp FBO
        ((renderLayerFn)g_pGlobalState->renderLayerHook->m_original)(thisptr, layerSurface, monitor, now, popups, lockscreen);

        // Post-surface: restore currentFB, apply glass masked by temp FBO alpha, blit surface
        CGlassLayerCompositeElement::SGlassLayerCompositeData postData{it->second, alpha};
        g_pHyprRenderer->m_renderPass.add(makeUnique<CGlassLayerCompositeElement>(postData));

        it->second->damageIfMoved();
        return;
    }

    // Call the original renderLayer
    ((renderLayerFn)g_pGlobalState->renderLayerHook->m_original)(thisptr, layerSurface, monitor, now, popups, lockscreen);
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

    static auto onOpen = Event::bus()->m_events.window.open.listen([&](PHLWINDOW w) { onNewWindow(w); });

    static auto onClose = Event::bus()->m_events.window.close.listen([&](PHLWINDOW w) { onCloseWindow(w); });

    // Clear pending presets before config re-parse, commit after
    static auto onPreConfigReload = Event::bus()->m_events.config.preReload.listen([&]() { clearPendingPresets(); });

    static auto onConfigReloaded = Event::bus()->m_events.config.reloaded.listen([&]() {
        commitPendingPresets();
        validateConfig();
        parseLayerNamespaceFilters();
    });


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
        onNewWindow(window);
    }

    // Hook renderLayer for layer surface glass support
    auto renderLayerMatches = HyprlandAPI::findFunctionsByName(PHANDLE, "renderLayer");
    for (const auto& match : renderLayerMatches) {
        // Match the overload: CHyprRenderer::renderLayer(PHLLS, PHLMONITOR, steady_tp, bool, bool)
        if (match.demangled.contains("renderLayer") && match.demangled.contains("LayerSurface")) {
            g_pGlobalState->renderLayerHook = HyprlandAPI::createFunctionHook(PHANDLE, match.address, (void*)hkRenderLayer);
            if (g_pGlobalState->renderLayerHook)
                g_pGlobalState->renderLayerHook->hook();
            break;
        }
    }

    if (!g_pGlobalState->renderLayerHook) {
        HyprlandAPI::addNotificationV2(PHANDLE, {
            {"text", std::string("[hyprglass] Could not hook renderLayer — layer glass disabled")},
            {"time", (uint64_t)5000},
            {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
        });
    }

    HyprlandAPI::reloadConfig();
    validateConfig();
    parseLayerNamespaceFilters();

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
    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassLayerPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassLayerCompositeElement");

    g_pGlobalState->layerSurfaces.clear();
    g_pGlobalState->shaderManager.destroy();
    g_pGlobalState.reset();
}
