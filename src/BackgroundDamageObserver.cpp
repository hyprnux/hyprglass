#include "BackgroundDamageObserver.hpp"

#include "GlassDecoration.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "WindowGeometry.hpp"

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/protocols/XDGShell.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/protocols/core/Subcompositor.hpp>
#include <hyprland/src/protocols/types/SurfaceRole.hpp>

#include <algorithm>
#include <exception>
#include <format>

using namespace Desktop::View;

namespace {
    // Hyprland's own T1-parent walk guards against a cycle in the parent chain.
    constexpr int MAX_TREE_DEPTH = 64;

    bool isEnabled() {
        return g_pGlobalState && !g_pGlobalState->observerListeners.empty();
    }

    // Root of the tree this surface belongs to: the window/layer/lock-screen root
    // surface, or the topmost popup of a layer-owned popup chain. Mirrors how
    // Hyprland propagates the T1 owner down subsurface and popup chains.
    SP<CWLSurfaceResource> ownerRootSurface(SP<CWLSurfaceResource> resource) {
        for (int depth = 0; resource && depth < MAX_TREE_DEPTH; ++depth) {
            const auto role = resource->m_role;
            if (role->role() == SURFACE_ROLE_SUBSURFACE) {
                const auto subsurface = static_cast<CSubsurfaceRole*>(role.get())->m_subsurface.lock();
                if (!subsurface)
                    return nullptr;
                resource = subsurface->m_parent.lock();
                continue;
            }

            if (role->role() == SURFACE_ROLE_XDG_SHELL) {
                const auto xdgSurface = static_cast<CXDGSurfaceRole*>(role.get())->m_xdgSurface.lock();
                const auto popup      = xdgSurface ? xdgSurface->m_popup.lock() : nullptr;
                if (!popup)
                    return resource;

                const auto parent = popup->m_parent.lock();
                if (!parent) // layer-shell popup: no toplevel above it
                    return resource;

                resource = parent->m_surface.lock();
                continue;
            }

            return resource;
        }

        return nullptr;
    }

    // Window owning this surface's tree; null for layers and layer-owned popups.
    PHLWINDOW ownerWindow(const PHLWINDOW& viewWindow, const SP<CWLSurfaceResource>& resource) {
        if (viewWindow)
            return viewWindow;

        const auto ownerWl = CWLSurface::fromResource(ownerRootSurface(resource));
        return ownerWl ? CWindow::fromView(ownerWl->view()) : nullptr;
    }

    // Mirrors Hyprland's own commit-time damage gates (Window.cpp, Subsurface.cpp,
    // Popup.cpp): a window that is unmapped, hidden or on an invisible workspace
    // draws nothing, so its commits cannot change what a layer samples. Surfaces
    // with no owning window (layers, layer-owned popups) are never gated.
    bool passesVisibilityGate(const PHLVIEW& view, const PHLWINDOW& window) {
        if (!window)
            return true;

        if (!window->m_isMapped)
            return false;

        if (view->type() == VIEW_TYPE_WINDOW && window->isHidden())
            return false;

        return !window->m_workspace || window->m_workspace->m_visible;
    }

    // A self-sampling window draws its own content into its glass, so a commit that
    // changes that content changes the whole pane. Hyprland only copies the damaged
    // region out of the offload framebuffer (OpenGL.cpp end()), so without widening
    // here the pane keeps its previous blurred self image outside the damaged rect.
    void damageSelfSamplingWindow(const SP<CWLSurfaceResource>& resource) {
        if (!g_pGlobalState || !g_pGlobalState->selfSampleConfigured || g_pGlobalState->decorations.empty())
            return;

        if (!resource->m_current.updated.bits.damage)
            return;

        const auto wlSurface = CWLSurface::fromResource(resource);
        if (!wlSurface)
            return;

        const auto view = wlSurface->view();
        if (!view || view->type() == VIEW_TYPE_LOCK_SCREEN)
            return;

        const auto window = ownerWindow(CWindow::fromView(view), resource);
        if (!window || !passesVisibilityGate(view, window))
            return;

        // Match the committing window to its decoration first: only that one can
        // want the widened damage, and only it is worth reading a value off.
        // markBackgroundDirty() (not damageEntire()): a plain damageEntire() only
        // widens screen damage and leaves m_hasCachedSample valid, so the B1 cache
        // would keep compositing the window's now-stale self-sampled content.
        if (auto* decoration = glassDecorationFor(window); decoration && decoration->lastSelfSample() > 0.0f)
            decoration->markBackgroundDirty();
    }

    // Signal callbacks run inside Hyprland's emit, so an escaping exception would
    // unwind through the compositor. Covers exceptions only, not asserts.
    bool listenerExceptionReported = false;

    void reportListenerException(std::string_view what) {
        if (listenerExceptionReported)
            return;

        listenerExceptionReported = true;
        HyprlandAPI::addNotificationV2(PHANDLE, {
            {"text", std::format("[{}] exception in surface listener: {}", PLUGIN_NAME, what)},
            {"time", (uint64_t)8000},
            {"color", CHyprColor{1.0, 0.8, 0.2, 1.0}},
        });
    }

    bool surfaceInTree(const SP<CWLSurfaceResource>& surface, const SP<CWLSurfaceResource>& root) {
        if (!root)
            return false;
        return root->findFirstPreorder([&surface](SP<CWLSurfaceResource> candidate) { return candidate == surface; }) != nullptr;
    }

    void onSurfaceCommit(const SP<CWLSurfaceResource>& resource) {
        if (!g_pGlobalState || !resource)
            return;

        const auto& config = g_pGlobalState->config;

        // Two independent sides can want this observer armed: layers (tracked
        // surfaces + layers:enabled) and windows (any decoration + windows:live_resample).
        // Early-out only when *neither* side has anything to gain from this commit.
        const bool layersTracked  = config.layersEnabled && **config.layersEnabled && !g_pGlobalState->layerSurfaces.empty();
        const bool windowsTracked = config.windowsLiveResample && **config.windowsLiveResample && !g_pGlobalState->decorations.empty();
        if (!layersTracked && !windowsTracked)
            return;

        // cheap skip when nothing can want a live resample
        const bool layersWantLive  = (config.layersLiveResample && **config.layersLiveResample) ||
            std::ranges::any_of(g_pGlobalState->layerNamespaceLiveResample, [](const auto& kv) { return kv.second; });
        const bool windowsWantLive = config.windowsLiveResample && **config.windowsLiveResample;
        if (!layersWantLive && !windowsWantLive)
            return;

        // same bit Hyprland tests: commits without damage change nothing behind us
        if (!resource->m_current.updated.bits.damage)
            return;

        const auto wlSurface = CWLSurface::fromResource(resource);
        if (!wlSurface)
            return;

        const auto view = wlSurface->view();
        // lock surfaces never reached the old damage path, and a glassed layer is
        // not sampled from under the lock
        if (!view || view->type() == VIEW_TYPE_LOCK_SCREEN)
            return;

        const auto viewWindow = CWindow::fromView(view); // non-null only for a window root
        // Resolved once, shared with the window loop's self-exclusion below.
        const auto committingWindow = ownerWindow(viewWindow, resource);
        if (!passesVisibilityGate(view, committingWindow))
            return;

        // nullopt for anything Hyprland would not render (and for IME popups, which
        // cannot be placed globally at all) — skipping is the only correct answer
        const auto box = wlSurface->getSurfaceBoxGlobal();
        if (!box.has_value())
            return;

        CRegion damage = wlSurface->computeDamage();
        if (damage.empty())
            return;

        // X11 clients draw at their own scale; only a window root carries it
        if (viewWindow && viewWindow->m_isX11 && viewWindow->m_X11SurfaceScaledBy != 1.f)
            damage.scale(1.0 / viewWindow->m_X11SurfaceScaledBy);

        // the animated origin, not Hyprland's animation goal: it is where the
        // content is actually drawn this frame
        damage.translate(box->pos());
        const CBox damagedBox = damage.getExtents();

        for (const auto& [_, state] : g_pGlobalState->layerSurfaces) {
            const auto layer = state->getLayerSurface();
            if (!layer || !layer->m_mapped)
                continue;

            if (!state->liveResampleEnabled())
                continue;

            // PROTOCOL_REGION: a commit overlapping only the non-region part of the
            // layer can't affect the glass sample, so test against the region's own
            // bounding box instead of the whole layer. ALPHA_THRESHOLD/NONE unchanged.
            const auto  monitor  = layer->m_monitor.lock();
            const float monScale = monitor ? monitor->m_scale : 1.0f;
            CBox sampleBox;
            if (state->resolveMaskSource() == CGlassLayerSurface::EMaskSource::PROTOCOL_REGION) {
                const auto regionBox = state->regionBoundingBoxGlobal();
                if (!regionBox)
                    continue; // no region to invalidate against
                sampleBox = *regionBox;
                // sampleBackground() pads whatever box it's given by
                // SAMPLE_PADDING_PX before blitting, so the real sampled/blurred
                // area reaches this far beyond the region box itself — without
                // this, a commit strictly inside that margin (but outside the
                // region) would never overlap and the stale sample would never
                // be marked dirty.
                sampleBox.expand(GlassRenderer::SAMPLE_PADDING_PX / monScale);
            } else {
                sampleBox = CBox{layer->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT),
                                  layer->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT)};
                sampleBox.expand(GlassRenderer::SAMPLE_PADDING_PX / monScale);
            }

            if (!sampleBox.overlaps(damagedBox))
                continue;

            // a layer's own content is not its background
            if (surfaceInTree(resource, layer->wlSurface() ? layer->wlSurface()->resource() : nullptr))
                continue;

            state->markBackgroundDirty();
        }

        // Single global switch: unlike layers, windows have no per-namespace
        // live-resample override to fall back on.
        if (windowsWantLive) {
            for (const auto& decorationRef : g_pGlobalState->decorations) {
                auto* decoration = decorationRef.get();
                if (!decoration)
                    continue;

                const auto window = decoration->getOwner();
                // Null (destroyed) or the committing surface's own tree: a
                // window's popups/subsurfaces are its own content, not its
                // background, regardless of tree depth.
                if (!window || window == committingWindow)
                    continue;

                const auto paddedBox = WindowGeometry::computePaddedGlobalBox(window, GlassRenderer::SAMPLE_PADDING_PX);
                if (!paddedBox || !paddedBox->overlaps(damagedBox))
                    continue;

                decoration->markBackgroundDirty();
            }
        }
    }

    void watchSurface(const SP<CWLSurfaceResource>& resource) {
        if (!g_pGlobalState || !resource)
            return;

        const WP<CWLSurfaceResource> key = resource;

        auto&                        watched = g_pGlobalState->watchedSurfaces;
        if (watched.contains(key))
            return;

        const auto wlSurface = CWLSurface::fromResource(resource);
        if (!wlSurface)
            return;

        auto& entry  = watched[key];
        entry.commit = resource->m_events.commit.listen([key] {
            try {
                if (const auto surface = key.lock()) {
                    damageSelfSamplingWindow(surface);
                    onSurfaceCommit(surface);
                }
            } catch (const std::exception& e) {
                reportListenerException(e.what());
            } catch (...) {
                reportListenerException("unknown exception");
            }
        });
        // erasing from inside the callback is safe: the emitting signal holds a
        // strong ref for the whole emit and the capture is by value
        entry.destroy = wlSurface->m_events.destroy.listen([key] {
            try {
                if (g_pGlobalState)
                    g_pGlobalState->watchedSurfaces.erase(key);
            } catch (const std::exception& e) {
                reportListenerException(e.what());
            } catch (...) {
                reportListenerException("unknown exception");
            }
        });
    }

    void watchView(const PHLVIEW& view) {
        if (!isEnabled() || !view)
            return;

        watchSurface(view->resource());
    }
}

void BackgroundDamageObserver::refreshEnabled() {
    if (!g_pGlobalState)
        return;

    // One predicate, computed here rather than duplicated per call site: any of
    // layers, windows live-resample or self-sample wanting invalidation is
    // reason enough to arm the observer, independent of the other two.
    const auto& config = g_pGlobalState->config;
    const bool layersWant  = config.layersEnabled && **config.layersEnabled;
    const bool windowsWant = config.windowsLiveResample && **config.windowsLiveResample;
    setEnabled(layersWant || windowsWant || g_pGlobalState->selfSampleConfigured);
}

void BackgroundDamageObserver::setEnabled(bool enabled) {
    if (!g_pGlobalState)
        return;

    if (!enabled) {
        g_pGlobalState->observerListeners.clear();
        g_pGlobalState->watchedSurfaces.clear();
        return;
    }

    if (isEnabled())
        return;

    // before the compositor protocol exists there is nothing to watch; leave
    // isEnabled() false so the next arm attempt retries
    if (!PROTO::compositor)
        return;

    // view.create covers windows, layers, popups, subsurfaces and lock surfaces,
    // and runs after Hyprland's own role commit handler
    g_pGlobalState->observerListeners.push_back(Event::bus()->m_events.view.create.listen([](const PHLVIEW& view) { watchView(view); }));
    // an XWayland window associates its wl_surface only after view.create, so its
    // root is reachable no earlier than the map
    g_pGlobalState->observerListeners.push_back(Event::bus()->m_events.window.open.listen([](PHLWINDOW window) { watchView(window); }));

    // View state registries cannot enumerate popups/subsurfaces, so sweep every
    // live surface and keep the ones that belong to a view.
    PROTO::compositor->forEachSurface([](SP<CWLSurfaceResource> resource) {
        const auto wlSurface = CWLSurface::fromResource(resource);
        if (wlSurface && wlSurface->view())
            watchSurface(resource);
    });
}
