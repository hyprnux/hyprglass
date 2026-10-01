#include "BackgroundDamageObserver.hpp"
#include "Diagnostics.hpp"
#include "GlassDecoration.hpp"
#include "GlassLayerCompositeElement.hpp"
#include "GlassLayerPassElement.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "PluginConfig.hpp"
#include "RenderGuards.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/event/EventBus.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
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

CGlassDecoration* glassDecorationFor(const PHLWINDOW& window) {
    for (const auto& decoration : g_pGlobalState->decorations) {
        auto* deco = decoration.get();
        if (deco && deco->ownsWindow(window))
            return deco;
    }
    return nullptr;
}

// Hyprland skips window decorations when internal fullscreen mode is
// FSMODE_FULLSCREEN, queue the glass pass from RENDER_PRE_WINDOW to avoid double-queue.
// The caller has already gated on the real monitor pass and locked both handles.
static void drawGlassForFullscreenWindow(const PHLWINDOW& window, const PHLMONITOR& monitor) {
    // decorations render normally, draw() already ran
    if (Fullscreen::controller()->getFullscreenModes(window).internal != Fullscreen::FSMODE_FULLSCREEN)
        return;

    // solitary frames render no background to sample
    if (monitor->m_solitaryClient.lock() == window)
        return;

    if (auto* deco = glassDecorationFor(window))
        deco->draw(monitor, 1.f); // alpha unused, recomputed in renderPass
}

// ── Duplicate window copies ──────────────────────────────────────────────────

// Every special workspace with alpha left gets its own pass over the same window
// list, and no render stage names the workspace a pass is drawing.
static bool otherSpecialWorkspaceVisible(const PHLWORKSPACE& workspace) {
    for (const auto& ref : State::workspaceState()->workspaces()) {
        const auto other = ref.lock();
        if (!other || other == workspace || !other->m_isSpecialWorkspace)
            continue;
        if (other->m_alpha->value() > 0.f)
            return true;
    }
    return false;
}

// Mirrors renderWorkspaceWindowsFullscreen(): a floating window allowed over
// fullscreen is rendered once below the fullscreen window and once above it.
// True only for the first of those two copies, false whenever anything is
// unclear — a wrong true drops a window for a frame.
// Caller established: real monitor pass, not a snapshot, and !isFullscreen(window).
// The fullscreen-window lookup gates the clauses that walk state
// (isFadingOutUnderFullscreen, shouldRenderOverFullscreen, shouldRenderWindow),
// so an ordinary window render pays member loads and that one lookup.
static bool isRedundantCopy(const PHLWINDOW& window, const PHLMONITOR& monitor) {
    const auto& dedupe = g_pGlobalState->dedupe;

    // the "and floating ones too" loop, below the fullscreen window
    if (!window->m_isFloating)
        return false;

    // pinned windows get a third render after the workspace passes, and nothing
    // in the render stages tells it apart from these two
    if (window->m_pinned)
        return false;

    // the "then render windows over fullscreen" loop must redraw it
    if (!window->m_isMapped)
        return false;

    // this must be the copy below the fullscreen window, which is rendered
    // between the two loops
    if (dedupe.sawFullscreen)
        return false;
    if (std::ranges::find(dedupe.dropped, window.get()) != dedupe.dropped.end())
        return false;

    const auto workspace = window->m_workspace;

    // the fullscreen render path runs for this workspace
    if (!workspace || workspace->m_monitor != monitor)
        return false;
    if (workspace != monitor->m_activeWorkspace && workspace != monitor->m_activeSpecialWorkspace)
        return false;

    // the loop between the two must find its fullscreen window, or it bails out
    // before the second one and this copy is the only one of the frame
    const auto fullscreenWindow = Fullscreen::controller()->getFullscreenWindow(workspace);
    if (!fullscreenWindow || fullscreenWindow->m_workspace != workspace || !Fullscreen::controller()->isFullscreen(fullscreenWindow))
        return false;

    if (window->m_monitor == workspace->m_monitor && workspace->m_isSpecialWorkspace != window->onSpecialWorkspace())
        return false;
    if (workspace->m_isSpecialWorkspace && (window->m_monitor != workspace->m_monitor || otherSpecialWorkspaceVisible(workspace)))
        return false;

    if (window->isFadingOutUnderFullscreen() || !window->shouldRenderOverFullscreen())
        return false;

    // the pre-filter both loops share
    if (window->alphaValue(Desktop::View::WINDOW_ALPHA_FADE) * window->alphaValue(Desktop::View::WINDOW_ALPHA_FULLSCREEN) == 0.f)
        return false;

    return g_pHyprRenderer->shouldRenderWindow(window, monitor);
}

static void beginWindowRender() {
    // the real monitor pass only: overview/screencopy framebuffers and snapshots
    // render the window once and have no scene behind to sample
    if (g_pHyprRenderer->m_renderData.projectionType != Render::RPT_MONITOR || g_pHyprRenderer->m_bRenderingSnapshot)
        return;

    const auto window  = g_pHyprRenderer->m_renderData.currentWindow.lock();
    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (!window || !monitor)
        return;

    // overview/thumbnail plugins render this window standalone into their own
    // framebuffer — a foreign replay must not touch dedupe bookkeeping either,
    // not just skip glass.
    if (RenderGuards::isForeignRender())
        return;

    auto& dedupe = g_pGlobalState->dedupe;

    // the flag only goes up and nothing after it is a candidate, so the
    // fullscreen lookup stops once the fullscreen window has been rendered
    if (!dedupe.sawFullscreen) {
        if (Fullscreen::controller()->isFullscreen(window))
            dedupe.sawFullscreen = true;
        // renderWindow queues its surfaces, shadow and border through
        // addPassElement, so redirecting the pass for its duration drops that
        // whole copy. Popups bypass it (m_renderPass.add) and still land under
        // the fullscreen window. Our own glass is skipped in queueGlassPass.
        else if (!dedupe.guard && isRedundantCopy(window, monitor)) {
            dedupe.sink.clear();
            dedupe.guard = g_pHyprRenderer->redirectPass(&dedupe.sink);
            dedupe.dropped.push_back(window.get());
        }
    }

    drawGlassForFullscreenWindow(window, monitor);
}

static void endWindowRender() {
    auto& dedupe = g_pGlobalState->dedupe;
    if (!dedupe.guard)
        return;

    dedupe.guard.reset();
    dedupe.sink.clear();
}

static void onRenderStage(eRenderStage stage) {
    if (!g_pGlobalState)
        return;

    switch (stage) {
        case RENDER_BEGIN:
            // one serial per monitor frame, including the solitary fast path
            // which emits no RENDER_PRE_WINDOWS
            ++g_pGlobalState->frameSerial;
            g_pGlobalState->dedupe.reset();
            if (const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock())
                Diagnostics::recordFrame(monitor->m_id);
            break;
        case RENDER_PRE_WINDOWS: g_pGlobalState->dedupe.resetEpoch(); break;
        case RENDER_PRE_WINDOW: beginWindowRender(); break;
        case RENDER_POST_WINDOW: endWindowRender(); break;
        // defensive: both stages are past the last renderWindow of the frame, so
        // a leaked guard is released and the sink drops its buffer references
        case RENDER_POST_WINDOWS: g_pGlobalState->dedupe.resetEpoch(); break;
        case RENDER_POST: g_pGlobalState->dedupe.reset(); break;
        default: break;
    }
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

    g_pGlobalState->layerNamespaceLiveResample.clear();
    parseKeyValuePairs(config.layersNamespaceLiveResample, '=', [&](const std::string& ns, const std::string& val) {
        if (val == "1" || val == "true" || val == "on" || val == "yes")
            g_pGlobalState->layerNamespaceLiveResample[ns] = true;
        else if (val == "0" || val == "false" || val == "off" || val == "no")
            g_pGlobalState->layerNamespaceLiveResample[ns] = false;
    });

    g_pGlobalState->layerNamespaceMaskModes.clear();
    parseKeyValuePairs(config.layersNamespaceMaskModes, '=', [&](const std::string& ns, const std::string& val) {
        if (auto mode = parseLayerMaskMode(val))
            g_pGlobalState->layerNamespaceMaskModes[ns] = *mode;
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

// Makes shouldBlur(PHLLS) return false for one renderLayer() call by mutating the
// surface state it reads; restored before any deferred pass element runs.
struct SLayerBlurSuppression {
    SP<Desktop::View::CWLSurface> surface;
    bool                          mutatedRegion    = false;
    bool                          mutatedHasEffect = false;
    CRegion                       savedRegion;

    explicit SLayerBlurSuppression(SP<Desktop::View::CWLSurface> wlSurface) : surface(std::move(wlSurface)) {
        if (!surface)
            return;
        if (surface->m_hasBackgroundEffect) {
            savedRegion = surface->m_blurRegion;
            surface->m_blurRegion.clear();
            mutatedRegion = true;
        } else {
            surface->m_hasBackgroundEffect = true;
            mutatedHasEffect               = true;
        }
    }

    ~SLayerBlurSuppression() {
        if (!surface)
            return;
        if (mutatedRegion)
            surface->m_blurRegion = savedRegion;
        if (mutatedHasEffect)
            surface->m_hasBackgroundEffect = false;
    }
};

// Both consumers of the surface observer in one place: layers when they are
// enabled, windows when any tier configures self_sample. Walks every preset, so
// it belongs only where the config can actually have changed.
static void refreshSurfaceObserver() {
    if (!g_pGlobalState)
        return;

    g_pGlobalState->selfSampleConfigured = anySelfSampleConfigured(g_pGlobalState->config, g_pGlobalState->customPresets);
    BackgroundDamageObserver::refreshEnabled();
}

using renderLayerFn = void (*)(Render::IHyprRenderer*, PHLLS, PHLMONITOR, const Time::steady_tp&, bool, bool);

static void hkRenderLayer(Render::IHyprRenderer* thisptr, PHLLS layerSurface, PHLMONITOR monitor,
                           const Time::steady_tp& now, bool popups, bool lockscreen) {
    const auto& config = g_pGlobalState->config;

    // layers:enabled can flip without a config reload (hyprctl keyword), so follow it
    // here too; this is a no-op once the observer is in the requested state.
    // self_sample is followed from the window path, which resolves it anyway.
    BackgroundDamageObserver::refreshEnabled();

    // Hyprland renders closing layers from snapshots. Do not inject the glass
    // pipeline while that snapshot is being captured: the snapshot framebuffer
    // starts transparent/black, so sampling it as a background can bake a black
    // rectangle into the fade-out snapshot.
    if (g_pHyprRenderer->m_bRenderingSnapshot) {
        ((renderLayerFn)g_pGlobalState->renderLayerHook->m_original)(thisptr, layerSurface, monitor, now, popups, lockscreen);
        return;
    }

    // Leave a foreign render entirely alone: no cache creation, no generation bump,
    // no layer registration. Its framebuffer holds content the real frame must not
    // inherit, and its geometry is not the one our caches are keyed on.
    if (RenderGuards::isForeignRender()) {
        ((renderLayerFn)g_pGlobalState->renderLayerHook->m_original)(thisptr, layerSurface, monitor, now, popups, lockscreen);
        return;
    }

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

        if (!layerSurface->m_mapped) {
            ((renderLayerFn)g_pGlobalState->renderLayerHook->m_original)(thisptr, layerSurface, monitor, now, popups, lockscreen);
            return;
        }

        float alpha = layerSurface->alpha().getTotal();
        if (alpha < 0.001f) {
            ((renderLayerFn)g_pGlobalState->renderLayerHook->m_original)(thisptr, layerSurface, monitor, now, popups, lockscreen);
            return;
        }

        // nothing to glass this frame (region mode without a bound region, or an
        // explicit empty region): render as Hyprland normally would
        const auto maskSource = it->second->resolveMaskSource();
        if (maskSource == CGlassLayerSurface::EMaskSource::NONE) {
            ((renderLayerFn)g_pGlobalState->renderLayerHook->m_original)(thisptr, layerSurface, monitor, now, popups, lockscreen);
            return;
        }

        // Pre-surface: sample+blur background, redirect currentFB → temp FBO
        CGlassLayerPassElement::SGlassLayerPassData preData{it->second, alpha};
        g_pHyprRenderer->m_renderPass.add(makeUnique<CGlassLayerPassElement>(preData));

        const bool manageBlur = config.layersManageBlur && **config.layersManageBlur;
        std::optional<SLayerBlurSuppression> blurSuppression;
        if (manageBlur) {
            if (auto wlSurface = layerSurface->wlSurface())
                blurSuppression.emplace(wlSurface);
        }

        // Original renderLayer: surface renders into the redirected temp FBO.
        // The suppression must wrap only this call: shouldBlur() runs inside it,
        // and the composite element must later see the real protocol state.
        ((renderLayerFn)g_pGlobalState->renderLayerHook->m_original)(thisptr, layerSurface, monitor, now, popups, lockscreen);
        blurSuppression.reset();

        // Post-surface: restore currentFB, apply glass masked by temp FBO alpha, blit surface
        CGlassLayerCompositeElement::SGlassLayerCompositeData postData{it->second, alpha, maskSource};
        g_pHyprRenderer->m_renderPass.add(makeUnique<CGlassLayerCompositeElement>(postData));

        it->second->damageIfMoved();
        return;
    }

    // Call the original renderLayer
    ((renderLayerFn)g_pGlobalState->renderLayerHook->m_original)(thisptr, layerSurface, monitor, now, popups, lockscreen);
}


// ── Margin safety check ──────────────────────────────────────────────────────

// The (margin, reach) pair from the last warning. A config.reloaded that leaves
// both Hyprland's blur settings and the plugin's own blur/refraction settings
// unchanged recomputes the same pair, so this suppresses renotifying every reload.
static std::optional<std::pair<float, float>> lastWarnedMarginReach;

// Hyprland's live-blur damage expansion (CRenderPass::render(), render/pass/Pass.cpp)
// is the only thing keeping the padded box this pipeline reads inside finalDamage.
// When the plugin's own reach exceeds that margin, finalDamage discards texels the
// glass shader still samples outside it, showing stale content at the window's
// edge instead of failing loudly.
static void checkBlurMarginSafety() {
    if (!g_pGlobalState)
        return;

    // Read the same way as the decoration:shadow:enabled check below: the plugin
    // has no cached pointer for Hyprland's own config, only for its own.
    const auto blurSizeValue   = Config::mgr()->getConfigValue("decoration:blur:size");
    const auto blurPassesValue = Config::mgr()->getConfigValue("decoration:blur:passes");
    auto* const PBLURSIZE   = reinterpret_cast<Hyprlang::INT* const*>(blurSizeValue.dataptr);
    auto* const PBLURPASSES = reinterpret_cast<Hyprlang::INT* const*>(blurPassesValue.dataptr);
    if (!PBLURSIZE || !PBLURPASSES)
        return;

    const auto& config = g_pGlobalState->config;
    if (!config.global.blurStrength || !config.global.blurIterations || !config.global.chromaticAberration || !config.global.refractionStrength)
        return;

    // Reproduces CRenderPass::oneBlurRadius() (render/pass/Pass.cpp) exactly.
    const int64_t blurSize      = std::clamp<int64_t>(**PBLURSIZE, 1, 40);
    const int64_t blurPasses    = std::clamp<int64_t>(**PBLURPASSES, 1, 8);
    const float   oneBlurRadius = static_cast<float>(blurSize) * std::pow(2.0f, static_cast<float>(blurPasses));
    const float   margin        = 1.5f * oneBlurRadius;

    const float blurStrength = **config.global.blurStrength;
    // Mirrors the clamp renderPass() applies to this same value at draw time
    // (GlassDecoration.cpp, GlassLayerSurface.cpp): the check must use the
    // iteration count that actually runs, not the raw unclamped config value.
    const int   iterations          = std::clamp(static_cast<int>(**config.global.blurIterations), 1, 5);
    const float chromaticAberration = **config.global.chromaticAberration;
    // Presets (e.g. the built-in "glass" preset, refraction_strength = 8.0) can raise
    // refraction_strength above this global value per window; this check only covers
    // the global layer, matching blurStrength/blurIterations/chromaticAberration above.
    const float refractionStrength = **config.global.refractionStrength;
    // Must mirror what renderPass() actually folds at draw time, or this reach
    // stops describing the texels the pipeline really reads.
    const bool  foldEnabled        = config.blurFold && **config.blurFold;
    const float reach              = GlassRenderer::sampleReachPx(blurStrength, iterations, chromaticAberration, refractionStrength, foldEnabled);

    if (margin >= reach) {
        lastWarnedMarginReach.reset(); // safe again; a later regression warns again
        return;
    }

    const auto currentPair = std::pair{margin, reach};
    if (lastWarnedMarginReach == currentPair)
        return; // already warned for this exact margin/reach combination
    lastWarnedMarginReach = currentPair;

    HyprlandAPI::addNotificationV2(PHANDLE, {
        {"text", std::format(
            "[hyprglass] decoration:blur:size={} / decoration:blur:passes={} give Hyprland only {:.0f}px of live-blur damage margin, "
            "but the glass effect can read up to {:.0f}px beyond a window's edge — raise blur:size/blur:passes or expect stale edges under motion.",
            blurSize, blurPasses, margin, reach)},
        {"time", (uint64_t)8000},
        {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
    });
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

    g_pGlobalState->listeners.push_back(Event::bus()->m_events.window.open.listen([&](PHLWINDOW w) { onNewWindow(w); }));

    g_pGlobalState->listeners.push_back(Event::bus()->m_events.window.close.listen([&](PHLWINDOW w) { onCloseWindow(w); }));

    g_pGlobalState->listeners.push_back(Event::bus()->m_events.layer.closed.listen([&](PHLLS layerSurface) { clearLayerGlassOnClose(layerSurface); }));

    // Z-order / visibility changes invalidate layer glass caches on the affected monitor only.
    // Per-monitor to avoid triggering re-samples on idle monitors (feedback loop).
    auto bumpWindowMonitor = [&](PHLWINDOW w) {
        if (w) if (auto mon = w->m_monitor.lock()) g_pGlobalState->bumpSceneGeneration(mon);
    };
    g_pGlobalState->listeners.push_back(Event::bus()->m_events.window.active.listen(
        [=](PHLWINDOW w, Desktop::eFocusReason) { bumpWindowMonitor(w); }));
    g_pGlobalState->listeners.push_back(Event::bus()->m_events.window.fullscreen.listen(
        [=](PHLWINDOW w) {
            bumpWindowMonitor(w);
            if (auto* deco = glassDecorationFor(w))
                deco->onFullscreenStateChanged();
        }));

    g_pGlobalState->listeners.push_back(Event::bus()->m_events.render.stage.listen(
        [](eRenderStage stage) { onRenderStage(stage); }));

    // Render-order fingerprint: reset the running hash at RENDER_BEGIN, then at
    // RENDER_LAST_MOMENT compare it to last frame's and bump scene generation on
    // a change. Both fire inside renderMonitor() strictly before endRender() runs
    // the render pass, so a bump here reaches this same frame's resample checks.
    g_pGlobalState->listeners.push_back(Event::bus()->m_events.render.stage.listen(
        [](eRenderStage stage) {
            if (stage != RENDER_BEGIN)
                return;
            if (const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock())
                g_pGlobalState->renderFingerprints[monitor->m_id].runningHash = 0;
        }));
    g_pGlobalState->listeners.push_back(Event::bus()->m_events.render.stage.listen(
        [](eRenderStage stage) {
            if (stage != RENDER_LAST_MOMENT)
                return;
            const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
            if (!monitor)
                return;
            auto& fingerprint = g_pGlobalState->renderFingerprints[monitor->m_id];
            if (fingerprint.runningHash == fingerprint.lastHash)
                return;
            g_pGlobalState->bumpSceneGeneration(monitor);
            fingerprint.lastHash = fingerprint.runningHash;
        }));
    g_pGlobalState->listeners.push_back(Event::bus()->m_events.window.moveToWorkspace.listen(
        [=](PHLWINDOW w, PHLWORKSPACE) { bumpWindowMonitor(w); }));
    g_pGlobalState->listeners.push_back(Event::bus()->m_events.workspace.active.listen(
        [&](PHLWORKSPACE ws) {
            if (ws) if (auto mon = ws->m_monitor.lock()) g_pGlobalState->bumpSceneGeneration(mon);
        }));

    // Clear pending presets/layers before config re-parse, commit after
    g_pGlobalState->listeners.push_back(Event::bus()->m_events.config.preReload.listen([&]() {
        clearPendingPresets();
        clearPendingLayers();
    }));

    g_pGlobalState->listeners.push_back(Event::bus()->m_events.config.reloaded.listen([&]() {
        if (!g_pGlobalState)
            return;
        initConfigPointers(PHANDLE, g_pGlobalState->config);
        commitPendingPresets();
        parseLayerNamespaceFilters();
        commitPendingLayers(); // merge Lua layer() calls on top of string config
        validateConfig();
        checkBlurMarginSafety();
        // config values are only valid here: reloadConfig() is asynchronous
        refreshSurfaceObserver();
    }));


    registerConfig(PHANDLE);
    initConfigPointers(PHANDLE, g_pGlobalState->config);
    Diagnostics::registerHyprCtlCommand(PHANDLE);

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
    bool renderLayerFound = false;
    auto renderLayerMatches = HyprlandAPI::findFunctionsByName(PHANDLE, "renderLayer");
    for (const auto& match : renderLayerMatches) {
        // Match the overload: Render::IHyprRenderer::renderLayer(PHLLS, PHLMONITOR, steady_tp, bool, bool)
        if (match.demangled.contains("renderLayer") && match.demangled.contains("LayerSurface")) {
            renderLayerFound = true;
            g_pGlobalState->renderLayerHook = HyprlandAPI::createFunctionHook(PHANDLE, match.address, (void*)hkRenderLayer);
            // createFunctionHook succeeds even when another plugin already owns the
            // address; only hook() reports that, and m_original then stays null
            if (g_pGlobalState->renderLayerHook && !g_pGlobalState->renderLayerHook->hook()) {
                HyprlandAPI::removeFunctionHook(PHANDLE, g_pGlobalState->renderLayerHook);
                g_pGlobalState->renderLayerHook = nullptr;
            }
            break;
        }
    }

    if (!g_pGlobalState->renderLayerHook) {
        HyprlandAPI::addNotificationV2(PHANDLE, {
            {"text", std::string(renderLayerFound ?
                "[hyprglass] Could not hook renderLayer (symbol found, hook failed — possibly already hooked by another plugin) — layer glass disabled" :
                "[hyprglass] Could not hook renderLayer (symbol not found) — layer glass disabled")},
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
    refreshSurfaceObserver();
    checkBlurMarginSafety();

    return {std::string(PLUGIN_NAME), std::string(PLUGIN_DESCRIPTION), std::string(PLUGIN_AUTHOR), std::string(PLUGIN_VERSION)};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (!g_pGlobalState)
        return;

    g_pGlobalState->listeners.clear();
    BackgroundDamageObserver::setEnabled(false);
    Diagnostics::unregisterHyprCtlCommand(PHANDLE);

    // drop the redirect and the sink's elements while the plugin is still mapped
    g_pGlobalState->dedupe.reset();

    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassLayerPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassLayerCompositeElement");

    Diagnostics::shutdown();

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
