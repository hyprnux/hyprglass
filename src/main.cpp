#include "BackgroundDamageObserver.hpp"
#include "Diagnostics.hpp"
#include "GlassDecoration.hpp"
#include "GlassLayerCompositeElement.hpp"
#include "GlassLayerPassElement.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "GlassSubsurfaceCompositeElement.hpp"
#include "GlassSubsurfacePassElement.hpp"
#include "GlassSubsurfaceState.hpp"
#include "Globals.hpp"
#include "ItemHints.hpp"
#include "PluginConfig.hpp"
#include "RenderGuards.hpp"
#include "SubsurfaceGeometry.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/pass/Pass.hpp>
#include <hyprland/src/render/pass/SurfacePassElement.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/event/EventBus.hpp>

#include <algorithm>
#include <cstdlib>
#include <dlfcn.h>
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

static void clearSubsurfaceFramebufferForMonitor(PHLMONITOR monitor) {
    if (!g_pGlobalState || !monitor)
        return;

    // The monitor is gone, so its temp FBO can never be reused: drop it
    // instead of keeping a dead entry around indefinitely.
    g_pGlobalState->subsurfaceTempFramebuffers.erase(monitor->m_id);
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
            // Subsurface item glass has no per-surface destroy signal to hang
            // an eager erase off (unlike layers/windows), so dead entries are
            // swept here instead — once per monitor frame, far cheaper than
            // the CRenderPass::add hook this state is populated from.
            std::erase_if(g_pGlobalState->subsurfaceGlass, [](const auto& pair) { return pair.first.expired(); });
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

// ── Subsurface item glass support ────────────────────────────────────────────
//
// A glass "item" (toolbar capsule, round button, ...) is a wl_subsurface of an
// app window that declares ext-background-effect-v1 on itself. Unlike layer
// glass (hooked at CHyprRenderer::renderLayer, one call per whole layer
// surface) an item is just one more CSurfacePassElement in a window's own
// surface tree (Renderer.cpp's breadthfirst walk over
// pWindow->wlSurface()->resource()), so the natural interception point is one
// level lower: Render::CRenderPass::add(UP<IPassElement>&&), which every pass
// element — surfaces, borders, shadows, our own layer elements — funnels
// through on its way into the pass.

using renderPassAddFn = void (*)(Render::CRenderPass*, UP<IPassElement>&&);

static void callOriginalRenderPassAdd(Render::CRenderPass* pass, UP<IPassElement>&& el) {
    ((renderPassAddFn)g_pGlobalState->renderPassAddHook->m_original)(pass, std::move(el));
}

// Same enable/disable resolution as the window's own glass (tags, global
// enabled, opaque-skip, ...) plus the subsurfaces:enabled kill switch — see
// CGlassDecoration::isGlassEnabled().
static bool subsurfaceGlassEnabledForWindow(const PHLWINDOW& window) {
    const auto& config = g_pGlobalState->config;
    if (!(config.subsurfacesEnabled && **config.subsurfacesEnabled))
        return false;
    if (!window)
        return false;

    auto* deco = glassDecorationFor(window);
    return deco && deco->isGlassEnabled();
}

static void hkRenderPassAdd(Render::CRenderPass* pass, UP<IPassElement>&& el) {
    // Hot path: every pass element of every kind (windows, layers, popups,
    // rects, cursors, snapshots...), on every monitor, every frame, funnels
    // through here — whether or not item glass is used anywhere. The
    // non-matching fast path must stay a couple of cheap branches, not a
    // cast chain or a fromResource/map lookup. Cheapest first: a plain enum
    // compare, before anything that calls into other systems.
    if (!el || el->type() != EK_SURFACE) {
        callOriginalRenderPassAdd(pass, std::move(el));
        return;
    }

    // Feature off (never enabled, or turned off this session): skip every
    // further check below, including the per-window glassDecorationFor()
    // lookup and any per-surface state map access — no item was ever
    // registered, so there is nothing further to look up.
    const auto& config = g_pGlobalState->config;
    if (!(config.subsurfacesEnabled && **config.subsurfacesEnabled)) {
        callOriginalRenderPassAdd(pass, std::move(el));
        return;
    }

    // Snapshots (closing-window/layer fade captures) start transparent/black —
    // sampling one as a background would bake that into the snapshot, same
    // reasoning as hkRenderLayer. A foreign render (overview/screencopy replay,
    // or one with its own render modifier already set) must not create or
    // touch any of our per-surface state either.
    if (g_pHyprRenderer->m_bRenderingSnapshot || RenderGuards::isForeignRender()) {
        callOriginalRenderPassAdd(pass, std::move(el));
        return;
    }

    // CSurfacePassElement is the only IPassElement whose type() returns
    // EK_SURFACE (checked above), so this cast can't fail.
    auto*       surfaceEl = static_cast<CSurfacePassElement*>(el.get());
    const auto& data      = surfaceEl->m_data;
    const auto  monitor   = data.pMonitor.lock();

    // Only a window's own non-main, non-popup child surfaces are candidates —
    // the window's main surface is the window itself (glassed by
    // CGlassDecoration already), and popups (menus, tooltips) are not items.
    if (data.mainSurface || data.popup || !data.pWindow || !data.surface || !monitor ||
        !subsurfaceGlassEnabledForWindow(data.pWindow)) {
        callOriginalRenderPassAdd(pass, std::move(el));
        return;
    }

    const auto wlSurface = Desktop::View::CWLSurface::fromResource(data.surface);
    if (!wlSurface || !wlSurface->m_hasBackgroundEffect || wlSurface->m_blurRegion.empty()) {
        // No longer eligible (effect object gone, or region cleared): drop any
        // existing state instead of leaving it to outlive the surface itself.
        g_pGlobalState->subsurfaceGlass.erase(WP<CWLSurfaceResource>(data.surface));
        callOriginalRenderPassAdd(pass, std::move(el));
        return;
    }

    // getTexBox() is exactly the box IPassElement::boundingBox() expects
    // (monitor-local logical, see SurfacePassElement.cpp) — capture it now,
    // before el's ownership moves into the pass below, and derive every other
    // coordinate family from it (see SubsurfaceGeometry.hpp).
    const CBox itemLogicalBox = surfaceEl->getTexBox();
    const auto pixelBox       = SubsurfaceGeometry::toPixelBox(itemLogicalBox, monitor);
    if (!pixelBox) {
        callOriginalRenderPassAdd(pass, std::move(el));
        return;
    }

    const CBox rawBox       = *pixelBox;
    const CBox transformBox = SubsurfaceGeometry::toTransformBox(rawBox, monitor);
    CRegion    region       = SubsurfaceGeometry::transformedItemBlurRegion(data.surface, rawBox, monitor);
    if (region.empty()) {
        // A background effect was declared but nothing is left to glass this
        // frame (e.g. an explicit empty region, or clipped fully outside the
        // subsurface's own committed size) — render the item exactly as
        // Hyprland would, and drop any existing state rather than let it
        // outlive the region that produced it.
        g_pGlobalState->subsurfaceGlass.erase(WP<CWLSurfaceResource>(data.surface));
        callOriginalRenderPassAdd(pass, std::move(el));
        return;
    }

    // Lazy per-surface state, keyed by a weak ref (see Globals.hpp) — pruned
    // once per monitor frame in onRenderStage(RENDER_BEGIN), not here: this
    // hook fires for every pass element in the frame and can't afford a map
    // sweep on each call.
    auto&                   states = g_pGlobalState->subsurfaceGlass;
    WP<CWLSurfaceResource>  key    = data.surface;
    auto                    it     = states.find(key);
    if (it == states.end())
        it = states.emplace(key, std::make_shared<CGlassSubsurfaceState>(key, data.pWindow)).first;

    const float alpha = std::clamp(data.alpha * data.fadeAlpha, 0.0f, 1.0f);

    // Pre-surface: sample+blur background, redirect currentFB → temp FBO.
    CGlassSubsurfacePassElement::SData preData{it->second, itemLogicalBox, transformBox, data.pMonitor, alpha};
    callOriginalRenderPassAdd(pass, makeUnique<CGlassSubsurfacePassElement>(preData));

    // The surface element itself: Hyprland's own draw() call renders it into
    // the temp FBO the pre-surface element just redirected currentFB to.
    callOriginalRenderPassAdd(pass, std::move(el));

    // Post-surface: restore currentFB, composite glass masked by the protocol
    // region with the item's own foreground (from the temp FBO) on top.
    CGlassSubsurfaceCompositeElement::SData postData{it->second, itemLogicalBox, rawBox, transformBox, std::move(region), data.pMonitor, alpha};
    callOriginalRenderPassAdd(pass, makeUnique<CGlassSubsurfaceCompositeElement>(postData));
}

// Only warn about the missing CRenderPass::add hook once config is actually
// loaded and the feature is turned on — called after every config reload so
// a user who enables subsurfaces later still gets told.
static void notifySubsurfaceHookFailure() {
    if (!g_pGlobalState || g_pGlobalState->renderPassAddHook || g_pGlobalState->subsurfaceHookFailureNotified)
        return;

    const auto& config = g_pGlobalState->config;
    if (!(config.subsurfacesEnabled && **config.subsurfacesEnabled))
        return;

    g_pGlobalState->subsurfaceHookFailureNotified = true;
    HyprlandAPI::addNotificationV2(PHANDLE, {
        {"text", std::string(g_pGlobalState->renderPassAddSymbolFound ?
            "[hyprglass] Could not hook CRenderPass::add (symbol found, hook failed — possibly already hooked by another plugin) — subsurface item glass disabled" :
            "[hyprglass] Could not hook CRenderPass::add (symbol not found) — subsurface item glass disabled")},
        {"time", (uint64_t)5000},
        {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
    });
}

// ── Hook target resolution ───────────────────────────────────────────────────

// Hyprland 0.56.2 symbols. Itanium mangling encodes the full signature, so a
// match is exactly the overload the hooks were written for.
namespace HookSymbols {
    inline constexpr auto RENDER_LAYER =
        "_ZN6Render13IHyprRenderer11renderLayerEN9Hyprutils6Memory14CSharedPointerIN7Desktop4View13CLayerSurfaceEEENS3_IN7Monitor8CMonitorEEERKNSt6chrono10time_"
        "pointINSB_3_V212steady_clockENSB_8durationIlSt5ratioILl1ELl1000000000EEEEEEbb";
    inline constexpr auto RENDER_PASS_ADD = "_ZN6Render11CRenderPass3addEON9Hyprutils6Memory14CUniquePointerI12IPassElementEE";
}

// dlsym first: findFunctionsByName() runs nm on the Hyprland binary, a fork from
// the compositor that slows down its startup. The dladdr check, against a
// function the Hyprland executable defines, rejects a same-named symbol from
// any other loaded object.
static void* resolveHyprlandFunction(const char* mangledName, const std::string& fallbackSearch, bool (*accept)(const SFunctionMatch&)) {
    Dl_info hyprland{};
    Dl_info symbol{};
    void*   address = dlsym(RTLD_DEFAULT, mangledName);
    if (address && dladdr(reinterpret_cast<void*>(&HyprlandAPI::findFunctionsByName), &hyprland) && dladdr(address, &symbol) && symbol.dli_fbase == hyprland.dli_fbase)
        return address;

    for (const auto& match : HyprlandAPI::findFunctionsByName(PHANDLE, fallbackSearch)) {
        if (accept(match))
            return match.address;
    }
    return nullptr;
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

    g_pGlobalState->listeners.push_back(Event::bus()->m_events.monitor.destroyMon.listen(
        [&](PHLMONITOR monitor) { clearSubsurfaceFramebufferForMonitor(monitor); }));

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
        // config values are only final here: Hyprland reloads the config after PLUGIN_INIT returns
        refreshSurfaceObserver();
        notifySubsurfaceHookFailure();
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
    // Fallback match for the overload Render::IHyprRenderer::renderLayer(PHLLS, PHLMONITOR, steady_tp, bool, bool)
    void* const renderLayerAddress = resolveHyprlandFunction(HookSymbols::RENDER_LAYER, "renderLayer", [](const SFunctionMatch& match) {
        return match.demangled.contains("renderLayer") && match.demangled.contains("LayerSurface");
    });
    const bool renderLayerFound = renderLayerAddress != nullptr;
    if (renderLayerFound) {
        g_pGlobalState->renderLayerHook = HyprlandAPI::createFunctionHook(PHANDLE, renderLayerAddress, (void*)hkRenderLayer);
        // createFunctionHook succeeds even when another plugin already owns the
        // address; only hook() reports that, and m_original then stays null
        if (g_pGlobalState->renderLayerHook && !g_pGlobalState->renderLayerHook->hook()) {
            HyprlandAPI::removeFunctionHook(PHANDLE, g_pGlobalState->renderLayerHook);
            g_pGlobalState->renderLayerHook = nullptr;
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

    // Hook CRenderPass::add for subsurface item glass support.
    // The fallback findFunctionsByName() greps the MANGLED symbol table (see
    // HyprlandAPI::findFunctionsByName, PluginAPI.cpp) — Itanium mangling has no
    // "::", so "CRenderPass::add" would never match there (only in the
    // demangled copy it separately keeps for the match's .demangled field).
    // "CRenderPass3add" is the mangled nested-name substring
    // (_ZN6Render11CRenderPass3addE...) and is confirmed unique in the running
    // binary; the demangled double-check below is what actually verifies it.
    void* const renderPassAddAddress = resolveHyprlandFunction(HookSymbols::RENDER_PASS_ADD, "CRenderPass3add", [](const SFunctionMatch& match) {
        return match.demangled.contains("CRenderPass::add") && match.demangled.contains("IPassElement");
    });
    if (renderPassAddAddress) {
        g_pGlobalState->renderPassAddHook = HyprlandAPI::createFunctionHook(PHANDLE, renderPassAddAddress, (void*)hkRenderPassAdd);
        if (g_pGlobalState->renderPassAddHook && !g_pGlobalState->renderPassAddHook->hook()) {
            HyprlandAPI::removeFunctionHook(PHANDLE, g_pGlobalState->renderPassAddHook);
            g_pGlobalState->renderPassAddHook = nullptr;
        }
    }

    // the failure notification waits for the config: see notifySubsurfaceHookFailure()
    g_pGlobalState->renderPassAddSymbolFound = renderPassAddAddress != nullptr;

    // No reloadConfig(): Hyprland reloads the config after every plugin load.
    // Until then the values parsed so far are committed here.
    initConfigPointers(PHANDLE, g_pGlobalState->config);
    commitPendingPresets();
    parseLayerNamespaceFilters();
    commitPendingLayers();
    validateConfig();
    refreshSurfaceObserver();
    notifySubsurfaceHookFailure();

    // Last: a PLUGIN_INIT that throws is unloaded without PLUGIN_EXIT, which
    // would leave the persistent helper calling back into this library.
    ItemHints::init();

    return {std::string(PLUGIN_NAME), std::string(PLUGIN_DESCRIPTION), std::string(PLUGIN_AUTHOR), std::string(PLUGIN_VERSION)};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (!g_pGlobalState)
        return;

    // Withdraws the global and clears every callback into this plugin before
    // anything else runs, since the helper library itself is never unloaded.
    ItemHints::exit();

    g_pGlobalState->listeners.clear();
    BackgroundDamageObserver::setEnabled(false);
    Diagnostics::unregisterHyprCtlCommand(PHANDLE);

    // drop the redirect and the sink's elements while the plugin is still mapped
    g_pGlobalState->dedupe.reset();

    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassLayerPassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassLayerCompositeElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassSubsurfacePassElement");
    g_pHyprRenderer->m_renderPass.removeAllOfType("CGlassSubsurfaceCompositeElement");

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

    if (g_pGlobalState->renderPassAddHook) {
        HyprlandAPI::removeFunctionHook(PHANDLE, g_pGlobalState->renderPassAddHook);
        g_pGlobalState->renderPassAddHook = nullptr;
    }

    g_pGlobalState->layerSurfaces.clear();
    g_pGlobalState->subsurfaceGlass.clear();
    g_pGlobalState->shaderManager.destroy();
    g_pGlobalState.reset();
}
