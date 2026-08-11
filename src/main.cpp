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

#include <cstdlib>
#include <sstream>

static void clearLayerGlassOnClose(PHLLS layerSurface) {
    if (!g_pGlobalState || !layerSurface)
        return;

    // Drop cached layer glass immediately. Otherwise the previous glass output
    // can remain in the damage history while Hyprland switches to its close
    // snapshot path, showing stale/black pixels for a frame.
    std::erase_if(g_pGlobalState->layerSurfaces, [&](const auto& pair) {
        return pair.first == layerSurface.get() || pair.second->getLayerSurface() == layerSurface;
    });

    if (auto monitor = layerSurface->m_monitor.lock())
        g_pHyprRenderer->damageMonitor(monitor);
}

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
        auto* deco = decoration.get();
        return !deco || deco->getOwner() == window;
    });
}

// ── Layer surface support ────────────────────────────────────────────────────

// Parse comma-separated config string into a set of trimmed values.
static void parseCommaSeparated(StringConfigPtr configPtr, std::unordered_set<std::string>& out) {
    out.clear();
    const auto raw = readStringConfig(configPtr);
    if (raw.empty()) return;

    std::istringstream stream{std::string(raw)};
    std::string token;
    while (std::getline(stream, token, ',')) {
        auto start = token.find_first_not_of(" \t");
        auto end   = token.find_last_not_of(" \t");
        if (start != std::string::npos)
            out.insert(token.substr(start, end - start + 1));
    }
}

// Parse comma-separated "key<sep>value" pairs. The callback receives (key, valueStr) for each pair.
template <typename Fn>
static void parseKeyValuePairs(StringConfigPtr configPtr, char separator, Fn&& callback) {
    const auto raw = readStringConfig(configPtr);
    if (raw.empty()) return;

    std::istringstream stream{std::string(raw)};
    std::string token;
    while (std::getline(stream, token, ',')) {
        auto sepPos = token.rfind(separator);
        if (sepPos == std::string::npos) continue;

        auto kStart = token.find_first_not_of(" \t");
        auto kEnd   = token.find_last_not_of(" \t", sepPos - 1);
        auto vStart = token.find_first_not_of(" \t", sepPos + 1);
        auto vEnd   = token.find_last_not_of(" \t");

        if (kStart != std::string::npos && kEnd != std::string::npos &&
            vStart != std::string::npos && vEnd != std::string::npos && kStart <= kEnd && vStart <= vEnd) {
            callback(token.substr(kStart, kEnd - kStart + 1),
                     token.substr(vStart, vEnd - vStart + 1));
        }
    }
}

static void parseLayerNamespaceFilters() {
    const auto& config = g_pGlobalState->config;
    parseCommaSeparated(config.layersNamespaces, g_pGlobalState->layerNamespaceFilter);
    parseCommaSeparated(config.layersExcludeNamespaces, g_pGlobalState->layerNamespaceExclude);

    g_pGlobalState->layerNamespacePresets.clear();
    parseKeyValuePairs(config.layersNamespacePresets, ':', [&](const std::string& ns, const std::string& preset) {
        g_pGlobalState->layerNamespacePresets.emplace(ns, preset);
    });

    g_pGlobalState->layerNamespaceMaskThresholds.clear();
    parseKeyValuePairs(config.layersNamespaceMaskThresholds, '=', [&](const std::string& ns, const std::string& val) {
        try { g_pGlobalState->layerNamespaceMaskThresholds.emplace(ns, std::stof(val)); } catch (...) {}
    });
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

using renderLayerFn = void (*)(Render::IHyprRenderer*, PHLLS, PHLMONITOR, const Time::steady_tp&, bool, bool);

static void hkRenderLayer(Render::IHyprRenderer* thisptr, PHLLS layerSurface, PHLMONITOR monitor,
                           const Time::steady_tp& now, bool popups, bool lockscreen) {
    const auto& config = g_pGlobalState->config;

    // Hyprland renders closing layers from snapshots. Do not inject the glass
    // pipeline while that snapshot is being captured: the snapshot framebuffer
    // starts transparent/black, so sampling it as a background can bake a black
    // rectangle into the fade-out snapshot.
    /*
    if (g_pHyprRenderer->m_bRenderingSnapshot) {
        ((renderLayerFn)g_pGlobalState->renderLayerHook->m_original)(thisptr, layerSurface, monitor, now, popups, lockscreen);
        return;
    }
    */

    // Prune dead layer surfaces whose weak_ptr has expired (layer was destroyed
    // but never got a replacement at the same raw pointer address)
    std::erase_if(g_pGlobalState->layerSurfaces, [](const auto& pair) {
        return !pair.second->getLayerSurface();
    });

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

        // Allow glass to keep rendering during close/fade-out animations.
        // sampleAndRedirect() already handles !m_mapped by reusing the cached
        // blurred background instead of re-sampling, so the glass effect stays
        // visible for the full duration of the exit animation.

        float alpha = layerSurface->alpha().getTotal();
        if (alpha < 0.001f) {
            ((renderLayerFn)g_pGlobalState->renderLayerHook->m_original)(thisptr, layerSurface, monitor, now, popups, lockscreen);
            return;
        }

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

    // Only compare the ABI suffix (e.g. "_aq_0.12_hu_0.13_hg_0.5_hc_0.1_hlg_0.6"),
    // not the leading commit hash. The commit hash changes with every git commit
    // but the ABI components determine actual binary compatibility.
    auto abiSuffix = [](const std::string& hash) -> std::string_view {
        auto pos = hash.find("_aq_");
        return pos != std::string::npos ? std::string_view{hash}.substr(pos) : std::string_view{hash};
    };

    if (abiSuffix(HASH) != abiSuffix(CLIENT_HASH)) {
        // Last-resort escape hatch for exotic setups: HYPRGLASS_SKIP_VERSION_CHECK
        // (set in Hyprland's own environment) downgrades the hard failure to a
        // warning. Unsupported — a real ABI mismatch can crash Hyprland.
        const char* skipEnv  = std::getenv("HYPRGLASS_SKIP_VERSION_CHECK");
        const bool  skip     = skipEnv && *skipEnv && std::string_view{skipEnv} != "0";
        if (!skip) {
            HyprlandAPI::addNotification(PHANDLE,
                std::format("[{}] Version mismatch! (plugin: {}, running: {})", PLUGIN_NAME, HASH, CLIENT_HASH),
                CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
            throw std::runtime_error("Version mismatch");
        }
        HyprlandAPI::addNotificationV2(PHANDLE, {
            {"text", std::format("[{}] Version mismatch ignored (HYPRGLASS_SKIP_VERSION_CHECK) — ABI differences may crash Hyprland", PLUGIN_NAME)},
            {"time", (uint64_t)8000},
            {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
        });
    }

    g_pGlobalState = std::make_unique<SGlobalState>();

    static auto onOpen = Event::bus()->m_events.window.open.listen([&](PHLWINDOW w) { onNewWindow(w); });

    static auto onClose = Event::bus()->m_events.window.close.listen([&](PHLWINDOW w) { onCloseWindow(w); });

    static auto onLayerClosed = Event::bus()->m_events.layer.closed.listen([&](PHLLS layerSurface) { clearLayerGlassOnClose(layerSurface); });

    // Z-order / visibility changes invalidate layer glass caches on the affected monitor only.
    // Per-monitor to avoid triggering re-samples on idle monitors (feedback loop).
    auto bumpWindowMonitor = [&](PHLWINDOW w) {
        if (w) if (auto mon = w->m_monitor.lock()) g_pGlobalState->bumpSceneGeneration(mon);
    };
    static auto onWindowActive = Event::bus()->m_events.window.active.listen(
        [=](PHLWINDOW w, Desktop::eFocusReason) { bumpWindowMonitor(w); });
    static auto onWindowFullscreen = Event::bus()->m_events.window.fullscreen.listen(
        [=](PHLWINDOW w) { bumpWindowMonitor(w); });
    static auto onWindowMoveToWorkspace = Event::bus()->m_events.window.moveToWorkspace.listen(
        [=](PHLWINDOW w, PHLWORKSPACE) { bumpWindowMonitor(w); });
    static auto onWorkspaceActive = Event::bus()->m_events.workspace.active.listen(
        [&](PHLWORKSPACE ws) {
            if (ws) if (auto mon = ws->m_monitor.lock()) g_pGlobalState->bumpSceneGeneration(mon);
        });

    // Clear pending presets/layers before config re-parse, commit after
    static auto onPreConfigReload = Event::bus()->m_events.config.preReload.listen([&]() {
        clearPendingPresets();
        clearPendingLayers();
    });

    static auto onConfigReloaded = Event::bus()->m_events.config.reloaded.listen([&]() {
        initConfigPointers(PHANDLE, g_pGlobalState->config);
        commitPendingPresets();
        parseLayerNamespaceFilters();
        commitPendingLayers(); // merge Lua layer() calls on top of string config
        validateConfig();
    });


    registerConfig(PHANDLE);
    initConfigPointers(PHANDLE, g_pGlobalState->config);

    // Shadows must be enabled for the glass effect to sample the correct background.
    // Force-enable if the user has disabled them.
    const auto shadowEnabled = Config::mgr()->getConfigValue("decoration:shadow:enabled");
    auto* const PSHADOWENABLED = reinterpret_cast<Hyprlang::INT* const*>(shadowEnabled.dataptr);
    if (PSHADOWENABLED && !**PSHADOWENABLED) {
        HyprlandAPI::invokeHyprctlCommand("keyword", "decoration:shadow:enabled true");
    }

    for (auto& window : Desktop::viewState()->windows()) {
        if (window->isHidden() || !window->m_isMapped)
            continue;
        onNewWindow(window);
    }

    // Hook renderLayer for layer surface glass support
    auto renderLayerMatches = HyprlandAPI::findFunctionsByName(PHANDLE, "renderLayer");
    for (const auto& match : renderLayerMatches) {
        // Match the overload: Render::IHyprRenderer::renderLayer(PHLLS, PHLMONITOR, steady_tp, bool, bool)
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
    initConfigPointers(PHANDLE, g_pGlobalState->config);
    commitPendingPresets();
    parseLayerNamespaceFilters();
    commitPendingLayers();
    validateConfig();

    return {std::string(PLUGIN_NAME), std::string(PLUGIN_DESCRIPTION), std::string(PLUGIN_AUTHOR), std::string(PLUGIN_VERSION)};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (!g_pGlobalState)
        return;

    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassLayerPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassLayerCompositeElement");

    for (auto& decoration : g_pGlobalState->decorations) {
        if (auto* deco = decoration.get())
            HyprlandAPI::removeWindowDecoration(PHANDLE, deco);
    }
    g_pGlobalState->decorations.clear();

    if (g_pGlobalState->renderLayerHook) {
        HyprlandAPI::removeFunctionHook(PHANDLE, g_pGlobalState->renderLayerHook);
        g_pGlobalState->renderLayerHook = nullptr;
    }

    g_pGlobalState->layerSurfaces.clear();
    g_pGlobalState->shaderManager.destroy();
    g_pGlobalState.reset();
}
