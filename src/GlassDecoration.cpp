#include "GlassDecoration.hpp"
#include "BackgroundDamageObserver.hpp"
#include "BuiltInPresets.hpp"
#include "Diagnostics.hpp"
#include "GlassPassElement.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "Hash.hpp"
#include "RenderGuards.hpp"
#include "WindowGeometry.hpp"
#include "WorkspaceAnimation.hpp"

#include <algorithm>
#include <GLES3/gl32.h>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/state/WindowState.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/config/shared/complex/ComplexDataTypes.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Region.hpp>

CGlassDecoration::CGlassDecoration(PHLWINDOW window)
    : IHyprWindowDecoration(window), m_window(window) {
}

CGlassDecoration::~CGlassDecoration() {
    withdrawNoBlur();
}

// Glass replaces Hyprland's blur for this window. Mark glassed windows with
// the noblur property so Hyprland composites their translucency against the
// live framebuffer (which contains the glass) instead of its pre-frame cached
// blur snapshot, which is captured before plugin decorations render (#46).
void CGlassDecoration::updateNoBlurProp(bool glassEnabled) {
    const auto& config = g_pGlobalState->config;
    const bool manage = config.manageWindowBlur && **config.manageWindowBlur;

    if (!manage || !glassEnabled) {
        withdrawNoBlur();
        return;
    }

    if (m_noBlurApplied)
        return;

    try {
        const auto window = m_window.lock();
        if (window && window->m_ruleApplicator) {
            window->m_ruleApplicator->noBlur().set(true, Desktop::Types::PRIORITY_SET_PROP);
            m_noBlurApplied = true;
            damageEntire();
        }
    } catch (...) {}
}

void CGlassDecoration::withdrawNoBlur() {
    if (!m_noBlurApplied)
        return;
    m_noBlurApplied = false;

    try {
        const auto window = m_window.lock();
        if (window && window->m_ruleApplicator) {
            window->m_ruleApplicator->noBlur().unset(Desktop::Types::PRIORITY_SET_PROP);
            damageEntire();
        }
    } catch (...) {}
}

// Fullscreen toggles re-apply window rules, which can drop the noblur prop
// while m_noBlurApplied still claims it's held.
void CGlassDecoration::onFullscreenStateChanged() {
    m_noBlurApplied = false;
    damageEntire();
}

static float selfSampleFor(const SResolveContext& ctx) {
    return std::clamp(resolvePresetFloat(ctx, &SPresetValues::selfSample, &SOverridableConfig::selfSample), 0.0f, 1.0f);
}

CGlassDecoration::EEnabledResolution CGlassDecoration::resolveEnabled() const {
    const auto& config = g_pGlobalState->config;
    const bool globalEnabled = config.enabled && **config.enabled;
    const bool skipOpaque    = config.skipOpaqueWindows && **config.skipOpaqueWindows;

    try {
        const auto window = m_window.lock();
        if (window && window->m_ruleApplicator) {
            const auto& tags = window->m_ruleApplicator->m_tagKeeper;
            // isTagged() already matches dynamic tags ("tag*") — no stripping needed here.
            // Disabled tag wins over enabled tag if both are present.
            if (tags.isTagged(std::string(TAG_DISABLED)))
                return EEnabledResolution::Disabled;
            if (tags.isTagged(std::string(TAG_ENABLED)))
                return EEnabledResolution::Enabled;
        }

        // A global disable must pre-empt the opaque-skip check below: otherwise
        // an opaque window with the plugin off entirely would still resolve to
        // DisabledBecauseOpaque and inflate the opaque-skip counter in draw().
        if (!globalEnabled)
            return EEnabledResolution::Disabled;

        // Nothing behind an opaque window is visible, unless the window
        // self-samples: then the glass shows the window's own content, which
        // does change, so the opaque-skip must yield to it.
        if (skipOpaque && window && window->opaque()) {
            const bool         isDark = resolveThemeIsDark();
            const std::string  preset = resolvePresetName();
            const SResolveContext ctx = {preset, isDark, config, g_pGlobalState->customPresets};
            if (selfSampleFor(ctx) <= 0.0f)
                return EEnabledResolution::DisabledBecauseOpaque;
        }
    } catch (...) {}

    return globalEnabled ? EEnabledResolution::Enabled : EEnabledResolution::Disabled;
}

bool CGlassDecoration::resolveThemeIsDark() const {
    try {
        const auto window = m_window.lock();
        if (window && window->m_ruleApplicator) {
            const std::string lightTag = std::string(TAG_THEME_PREFIX) + "light";
            const std::string darkTag  = std::string(TAG_THEME_PREFIX) + "dark";
            if (window->m_ruleApplicator->m_tagKeeper.isTagged(lightTag))
                return false;
            if (window->m_ruleApplicator->m_tagKeeper.isTagged(darkTag))
                return true;
        }

        const auto& config = g_pGlobalState->config;
        const auto theme = readStringConfig(config.defaultTheme);
        if (!theme.empty())
            return theme != "light";
    } catch (...) {}

    return true;
}

std::string CGlassDecoration::resolvePresetName() const {
    try {
        const auto window = m_window.lock();
        if (window && window->m_ruleApplicator) {
            for (const auto& tag : window->m_ruleApplicator->m_tagKeeper.getTags()) {
                if (tag.starts_with(TAG_PRESET_PREFIX))
                    return stripDynamicTagMarker(tag.substr(TAG_PRESET_PREFIX.size()));
            }
        }

        const auto& config = g_pGlobalState->config;
        const auto preset = readStringConfig(config.defaultPreset);
        if (!preset.empty())
            return std::string(preset);
    } catch (...) {}

    return "default";
}

SDecorationPositioningInfo CGlassDecoration::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.priority       = 10000;
    info.policy         = DECORATION_POSITION_ABSOLUTE;
    // The overlap shadow reaches this far outside the window; reporting it
    // makes Hyprland's own window damage (focus changes, moves) include it.
    const double r      = overlapShadowRange();
    info.desiredExtents = {{r, r}, {r, r}};
    return info;
}

void CGlassDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {}

void CGlassDecoration::queueGlassPass(float alpha, bool glass, bool shadow, const std::vector<CBox>& beneath) {
    // A duplicate copy is redirected into the dedupe sink and dropped whole: it
    // needs no glass, and stamping it would leave the surviving element stale.
    if (g_pGlobalState->dedupe.guard)
        return;

    CGlassPassElement::SGlassPassData data{m_self, alpha};
    data.glass   = glass;
    data.shadow  = shadow;
    data.beneath = shadow ? beneath : std::vector<CBox>{};

    // Only the real monitor pass is de-duplicated: snapshots, screencopy and
    // overview framebuffers render the window once, out of frame order.
    const bool managed = g_pGlobalState->frameSerial != 0 && !g_pHyprRenderer->m_bRenderingSnapshot &&
        g_pHyprRenderer->m_renderData.projectionType == Render::RPT_MONITOR;

    if (managed) {
        if (m_glassFrameSerial != g_pGlobalState->frameSerial) {
            m_glassFrameSerial = g_pGlobalState->frameSerial;
            m_glassQueueIndex  = 0;
        } else
            ++m_glassQueueIndex;

        data.frameSerial = m_glassFrameSerial;
        data.queueIndex  = m_glassQueueIndex;
    }

    // m_renderPass, never addPassElement: draw() runs inside Hyprland's own
    // per-window redirect for transformed windows (motion blur), whose pass
    // renders into a work buffer cleared to transparent — nothing to sample.
    g_pHyprRenderer->m_renderPass.add(makeUnique<CGlassPassElement>(data));
}

void CGlassDecoration::draw(PHLMONITOR monitor, float const& alpha) {
    if (!g_pGlobalState)
        return;

    // Render-order fingerprint: fold this window's identity/geometry/alpha into
    // the monitor's running hash in z-order, before resolveEnabled() so background
    // windows still count. Guarded by isForeignRender(), not a bare mainFB check,
    // since a foreign replay's z-order isn't this frame's real one. Folded at most
    // once per frameSerial: draw() itself runs 2-3x per monitor frame for a floating
    // window over fullscreen, and folding every copy would make the hash depend on
    // how many of those copies happened to render this frame rather than on the scene.
    if (monitor && !RenderGuards::isForeignRender() &&
        (g_pGlobalState->frameSerial == 0 || m_lastFoldedFrameSerial != g_pGlobalState->frameSerial)) {
        if (const auto window = m_window.lock()) {
            const auto workspace = window->m_workspace;
            const Vector2D workspaceRenderOffset =
                (workspace && !window->m_pinned) ? workspace->m_renderOffset->value() : Vector2D();
            const auto fullscreenMode = Fullscreen::controller()->getFullscreenModes(window).internal;

            auto& fingerprint = g_pGlobalState->renderFingerprints[monitor->m_id];
            Hash::hashCombine(fingerprint.runningHash, window.get(), window->positionAnimation()->value(),
                               window->sizeAnimation()->value(), alpha, fullscreenMode, workspaceRenderOffset);

            m_lastFoldedFrameSerial = g_pGlobalState->frameSerial;
        }
    }

    const auto enabledResolution = resolveEnabled();
    const bool enabled = enabledResolution == EEnabledResolution::Enabled;
    updateNoBlurProp(enabled);
    if (!enabled) {
        m_lastSelfSample = 0.0f;

        // Only count a skip that resolveEnabled() itself attributes to
        // skip_opaque_windows — a tag or global-disable skip doesn't have a
        // counter of its own (see EEnabledResolution) and isn't counted here.
        if (monitor && enabledResolution == EEnabledResolution::DisabledBecauseOpaque)
            Diagnostics::recordWindowOpaqueSkipped(monitor->m_id);
    }

    // A foreign render must not queue a pass element at all: it would sample its own
    // framebuffer into this window's real, persistent background cache. It must not
    // touch the shadow state below either — its z-order is not this frame's.
    if (RenderGuards::isForeignRender())
        return;

    // Overlap shadow is independent of the glass: an unfocused window that
    // covers another one gets it whether or not it is glassed.
    std::vector<CBox> beneath;
    const bool        shadow = overlapShadowRange() > 0 && resolveOverlapShadow(monitor, &beneath);
    {
        std::vector<std::array<int, 4>> rects;
        for (const auto& b : beneath)
            rects.push_back({static_cast<int>(b.x), static_cast<int>(b.y), static_cast<int>(b.w), static_cast<int>(b.h)});
        if (shadow != m_lastShadow || rects != m_lastShadowClip) {
            m_lastShadow     = shadow;
            m_lastShadowClip = std::move(rects);
            damageEntire();
        }
    }

    if (!enabled && !shadow)
        return;

    queueGlassPass(alpha, enabled, shadow, beneath);

    // A slide translates the scene under us without any geometry change, and
    // Hyprland's per-tick window damage carries none of our sampling padding.
    // Damage only: no cache state may be touched from a render.
    const auto window = m_window.lock();
    if (window && !window->m_pinned) {
        const auto workspace = window->m_workspace;
        if (workspace && workspace->m_renderOffset->isBeingAnimated())
            damageEntire();
    }
}

PHLWINDOW CGlassDecoration::getOwner() {
    return m_window.lock();
}

void CGlassDecoration::markBackgroundDirty() {
    if (m_backgroundDirty)
        return;

    const auto& config = g_pGlobalState->config;
    const int64_t fps = config.windowsLiveResampleFps ? **config.windowsLiveResampleFps : 0;
    const auto now = std::chrono::steady_clock::now();
    if (fps > 0 && now - m_lastDirtyMark < std::chrono::nanoseconds(1'000'000'000 / fps))
        return;
    m_lastDirtyMark = now;

    m_backgroundDirty = true;
    // damage the full sample region: outside the committed area the framebuffer
    // still holds our previous glass output, which must not be re-sampled
    damageEntire();
}

bool CGlassDecoration::wantsBackgroundResample(PHLMONITOR monitor, const CBox& transformBox) const {
    const auto& config = g_pGlobalState->config;
    if (!config.windowsBackgroundCache || !**config.windowsBackgroundCache)
        return true; // kill switch off: exact pre-cache behavior, no other state consulted

    if (!m_hasCachedSample)
        return true; // first frame: nothing to reuse yet

    // Per-monitor generation (see m_lastGenerationMonitor's declaration):
    // covers future commit/close-behind-cache invalidation as well as
    // existing event bumps, and forces a resample the instant a window
    // lands on a different monitor even if that monitor's counter happens
    // to numerically match the cached value.
    if (!monitor || monitor->m_id != m_lastGenerationMonitor || g_pGlobalState->getSceneGeneration(monitor) != m_lastSceneGeneration)
        return true;

    if (m_backgroundDirty)
        return true; // markBackgroundDirty() mark not yet escalated into a scene-generation bump

    const auto window = m_window.lock();
    if (!window)
        return true;

    // Own move/resize animation, re-checked every call rather than latched
    // once: updateWindow() bumps scene generation only at the animation's
    // start (no per-tick decoration callback exists), so every later frame of
    // the same still-interpolating animation needs this live poll to keep
    // resampling.
    if (window->positionAnimation()->isBeingAnimated() || window->sizeAnimation()->isBeingAnimated())
        return true;

    // A workspace slide or fade moves the whole scene behind us without any
    // geometry change of our own.
    if (WorkspaceAnimation::anyWorkspaceAnimating(monitor))
        return true;

    const auto source = g_pHyprRenderer->m_renderData.currentFB;
    if (!m_sampleFramebuffer || !source)
        return true;

    // Monitor/scale/DPI/HDR change: recompute the size sampleBackground()
    // would allocate this frame (never cached) and compare against the FBO's
    // actual size/format from the last real sample.
    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext ctx  = {preset, isDark, config, g_pGlobalState->customPresets};
    const float blurStrength   = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
    const int   downscale     = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

    const int fullWidth  = static_cast<int>(transformBox.w) + 2 * GlassRenderer::SAMPLE_PADDING_PX;
    const int fullHeight = static_cast<int>(transformBox.h) + 2 * GlassRenderer::SAMPLE_PADDING_PX;
    const int requiredWidth  = std::max(1, fullWidth / downscale);
    const int requiredHeight = std::max(1, fullHeight / downscale);

    if (m_sampleFramebuffer->m_size.x != requiredWidth || m_sampleFramebuffer->m_size.y != requiredHeight ||
        m_sampleFramebuffer->m_drmFormat != source->m_drmFormat)
        return true;

    return false;
}

int CGlassDecoration::overlapShadowRange() {
    if (!g_pGlobalState)
        return 0;
    const auto& config = g_pGlobalState->config;
    return config.overlapShadowRange ? static_cast<int>(std::max<Hyprlang::INT>(0, **config.overlapShadowRange)) : 0;
}

bool CGlassDecoration::resolveOverlapShadow(PHLMONITOR monitor, std::vector<CBox>* beneath) const {
    const auto window = m_window.lock();
    if (!window || !monitor || !window->m_workspace)
        return false;

    try {
        // The focused window keeps Hyprland's own shadow; a fullscreen one
        // covers everything and casts nothing.
        if (Desktop::focusState()->window() == window || Fullscreen::controller()->isFullscreen(window))
            return false;

        const auto optBox = WindowGeometry::computeWindowBox(window, monitor);
        if (!optBox)
            return false;
        CBox shadowBox = *optBox;
        shadowBox.expand(overlapShadowRange() * monitor->m_scale);

        // Hyprland's render order: the workspace's tiled windows, then its
        // floating ones, then the special workspace (tiled, floating), then
        // pinned windows; within a class, the window list order.
        auto rank = [](const PHLWINDOW& w) {
            if (w->m_pinned)
                return 4;
            const bool special = w->m_workspace && w->m_workspace->m_isSpecialWorkspace;
            return (special ? 2 : 0) + (w->m_isFloating ? 1 : 0);
        };
        const int   selfRank   = rank(window);
        const auto& windows    = Desktop::windowState()->windows();
        bool        found      = false;
        bool        passedSelf = false;

        for (const auto& other : windows) {
            if (other == window) {
                passedSelf = true;
                continue;
            }
            if (!other || !other->m_isMapped || other->isHidden() || !other->m_workspace)
                continue;
            const auto ws = other->m_workspace;
            const bool visibleHere = other->m_monitor == monitor && (ws == monitor->m_activeWorkspace || ws == monitor->m_activeSpecialWorkspace);
            if (!visibleHere)
                continue;
            const int otherRank = rank(other);
            bool      isBeneath;
            if (otherRank != selfRank)
                isBeneath = otherRank < selfRank;
            else if (!window->m_isFloating && !other->m_isFloating)
                isBeneath = false; // two tiled windows never overlap
            else
                isBeneath = !passedSelf; // same class: list order
            if (!isBeneath)
                continue;

            const auto otherBox = WindowGeometry::computeWindowBox(other, monitor);
            if (!otherBox)
                continue;
            CBox hit = *otherBox;
            if (hit.intersection(shadowBox).empty())
                continue;
            found = true;
            if (!beneath)
                return true;
            beneath->push_back(hit);
        }
        return found;
    } catch (...) {}
    return false;
}

void CGlassDecoration::drawOverlapShadow(PHLMONITOR monitor, const CBox& windowBox, const std::vector<CBox>& beneath, float alpha) {
    const auto window = m_window.lock();
    if (!window || !monitor)
        return;
    const auto& config = g_pGlobalState->config;
    const int   range  = overlapShadowRange();
    if (range <= 0)
        return;

    const float scale = monitor->m_scale;
    CBox        fullBox = windowBox;
    fullBox.expand(range * scale);

    // The pane's own box is taken out as a CROSS — the box inset by the
    // corner radius on each axis — so the four corner squares stay in: there
    // the shader cuts out the pane's real rounded shape (it knows the window
    // through m_renderData.currentWindow). Taking the whole rectangle out
    // left the corner patches between arc and box without any shadow, a
    // light square poking out of every rounded corner.
    const float rounding      = window->rounding() * scale;
    const float roundingPower = window->roundingPower(); // the cutout and the falloff share it; must match the pane
    const float r             = std::min({rounding, static_cast<float>(windowBox.w / 2), static_cast<float>(windowBox.h / 2)});
    CRegion     cross;
    cross.add(CBox{windowBox.x + r, windowBox.y, windowBox.w - 2 * r, windowBox.h});
    cross.add(CBox{windowBox.x, windowBox.y + r, windowBox.w, windowBox.h - 2 * r});

    const uint32_t   rgba  = config.overlapShadowColor ? static_cast<uint32_t>(**config.overlapShadowColor) : 0x00000040u;
    const CHyprColor color{((rgba >> 24) & 0xff) / 255.f, ((rgba >> 16) & 0xff) / 255.f, ((rgba >> 8) & 0xff) / 255.f, (rgba & 0xff) / 255.f};
    const Config::CGradientValueData grad{color};

    // renderRoundedShadow ignores any scissor set by the caller: it derives
    // its own draw region from the frame damage and scissors per damage rect
    // itself. So clip by handing it the damage already intersected with the
    // wanted region, draw, and put the damage back. It also needs to know
    // which window it is shading so the shader cuts the pane's own (rounded)
    // box out instead of painting the interior; that is
    // m_renderData.currentWindow, which a plugin pass element does not get.
    auto&         renderData  = g_pHyprRenderer->m_renderData;
    const CRegion savedDamage = renderData.damage;
    const auto    savedWindow = renderData.currentWindow;
    auto          drawIn      = [&](CRegion region, float a) {
        region.intersect(fullBox);
        region.subtract(cross);
        region.intersect(savedDamage);
        if (region.empty())
            return;
        renderData.damage        = region;
        renderData.currentWindow = window;
        g_pHyprRenderer->drawShadow(fullBox, static_cast<int>(rounding), roundingPower, static_cast<int>(range * scale), grad, a);
    };

    const bool clipToBeneath = config.overlapShadowClip && **config.overlapShadowClip;
    if (!clipToBeneath) {
        // The full ring around the pane.
        drawIn(CRegion{fullBox}, alpha);
    } else {
        // Contact shadow: full strength wherever the pane lies over a window
        // beneath, cut at that window's outline; the part of the pane resting
        // on the wallpaper casts nothing.
        CRegion region;
        for (const auto& box : beneath)
            region.add(box);
        drawIn(region, alpha);
    }
    renderData.currentWindow = savedWindow;
    renderData.damage        = savedDamage;
}

void CGlassDecoration::renderPass(PHLMONITOR monitor, const float& alpha, bool glass, bool shadow, const std::vector<CBox>& beneath) {
    // Belt and braces: draw() already refuses to queue a pass element for a foreign
    // render, but a caller that reaches renderPass() during one anyway must not sample
    // the foreign framebuffer into this decoration's persistent background cache.
    if (RenderGuards::isForeignRender())
        return;

    auto& shaderManager = g_pGlobalState->shaderManager;
    shaderManager.initializeIfNeeded();

    if (!shaderManager.isInitialized())
        return;

    const auto window = m_window.lock();
    if (!window)
        return;

    const auto source = g_pHyprRenderer->m_renderData.currentFB;
    if (!source)
        return;

    // The overlap shadow is drawn AFTER the pane (see the end of this
    // function): the pane paints its padded margin, which would otherwise
    // erase the ring.
    auto drawShadowIfWanted = [&]() {
        if (!shadow || beneath.empty())
            return;
        if (const auto box = WindowGeometry::computeWindowBox(window, monitor); box) {
            float a = window->alphaTotalWithout(Desktop::View::WINDOW_ALPHA_ACTIVE);
            if (const auto workspace = window->m_workspace; workspace && !window->m_pinned)
                a *= workspace->m_alpha->value();
            drawOverlapShadow(monitor, *box, beneath, a);
        }
    };
    if (!glass) {
        drawShadowIfWanted();
        return;
    }

    auto optBox = WindowGeometry::computeWindowBox(window, monitor);
    if (!optBox)
        return;

    if (monitor)
        Diagnostics::recordWindowGlassDraw(monitor->m_id);

    CBox windowBox    = *optBox;
    CBox transformBox = WindowGeometry::applyMonitorTransform(windowBox, monitor);

    // Every non-discarded frame needs this regardless of cache hit/miss: a
    // cache-hit frame has no sampleBackground()/blurBackground() call for it
    // to sit after, and reusing a stale value here (e.g. glassAlpha after a
    // fade, or cornerRadius after a fullscreen toggle) would silently
    // mis-render the composite even though the cached sample itself is fine.
    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    float monitorScale = monitor->m_scale;

    // Hyprland renders internal-fullscreen windows unrounded (dontRound), we need to
    // match, or the glass would show rounded gaps at the screen corners
    const bool fsUnrounded = Fullscreen::controller()->getFullscreenModes(window).internal == Fullscreen::FSMODE_FULLSCREEN;
    float cornerRadius  = fsUnrounded ? 0.0f : window->rounding() * monitorScale;
    float roundingPower = window->roundingPower();

    // Resolved here, not inside the cache branch below, so it stays fresh on a
    // cache-hit frame too, when neither sampleBackground() nor blendOwnContent() run.
    m_lastSelfSample = selfSampleFor(ctx);
    if (m_lastSelfSample > 0.0f && !g_pGlobalState->selfSampleConfigured) {
        // hyprctl keyword emits no config.reloaded, and a setup without layer
        // surfaces has no other place that would notice self_sample turning on
        g_pGlobalState->selfSampleConfigured = true;
        BackgroundDamageObserver::refreshEnabled();
    }

    // The render alpha Hyprland hands decorations is activeInactive * fade.
    // Glass must follow fades (open/close, fullscreen, workspace moves) but
    // not the active/inactive dimming or opacity rules: those make the surface
    // more translucent — revealing more glass — and shouldn't wash out the
    // glass pane itself. Rebuild the fade-only alpha from its components.
    float glassAlpha = window->alphaTotalWithout(Desktop::View::WINDOW_ALPHA_ACTIVE);
    if (const auto workspace = window->m_workspace; workspace && !window->m_pinned)
        glassAlpha *= workspace->m_alpha->value();

    const MONITORID monitorId = monitor ? monitor->m_id : -1; // -1 mirrors Hyprland's own MONITOR_INVALID

    if (!wantsBackgroundResample(monitor, transformBox)) {
        // Background unchanged since the last real sample — reuse it, skip
        // the most expensive GPU work (blit + blur passes) entirely.
        Diagnostics::recordWindowCacheHit(monitorId);
    } else {
        // sampleBackground()'s own padding math, in the same (physical,
        // post-transform) space as transformBox — no /scale, unlike the
        // logical-space padding boundingBox() uses.
        CBox paddedBox = transformBox;
        paddedBox.expand(GlassRenderer::SAMPLE_PADDING_PX);
        const bool covered = CRegion(paddedBox).subtract(g_pHyprRenderer->m_renderData.damage).empty();

        if (covered) {
            float blurStrength   = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
            int downscale        = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

            GlassRenderer::sampleBackground(m_sampleFramebuffer, source, transformBox, m_samplePaddingRatio, downscale);

            // Only here, on a real (non-cached) sample: blending onto a cache-hit
            // frame would double-composite our own content over an already-blurred FBO.
            if (m_lastSelfSample > 0.0f)
                GlassRenderer::blendOwnContent(m_sampleFramebuffer, window, monitor, transformBox, downscale,
                                               m_lastSelfSample, cornerRadius, roundingPower);

            float blurRadius     = blurStrength * 12.0f / downscale;
            int blurIterations   = std::clamp(static_cast<int>(resolvePresetInt(ctx, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);

            if (ctx.config.blurFold && **ctx.config.blurFold) {
                const GlassRenderer::SFoldedBlur folded = GlassRenderer::foldBlurPasses(blurRadius, blurIterations);
                blurRadius     = folded.radius;
                blurIterations = folded.iterations;
            }

            GlassRenderer::blurBackground(m_sampleFramebuffer, blurRadius, blurIterations, source);

            m_hasCachedSample       = true;
            m_lastSceneGeneration   = g_pGlobalState->getSceneGeneration(monitor);
            m_lastGenerationMonitor = monitorId;
            m_backgroundDirty       = false;
            Diagnostics::recordWindowCacheMiss(monitorId);
        } else if (m_hasCachedSample) {
            // Not enough of the padded box is damaged yet to safely re-sample
            // (would pick up stale pixels outside this frame's damage).
            // Force it into next frame's damage and draw the stale cache for
            // now — a future pass scissors the draw to what's actually damaged.
            damageEntire();
            Diagnostics::recordWindowDeferredResample(monitorId);
        } else {
            // No cache yet and not enough damage to sample cleanly: nothing
            // valid to draw this frame. The shadow does not depend on the
            // sample, so it still goes down.
            damageEntire();
            drawShadowIfWanted();
            return;
        }
    }

    GlassRenderer::applyGlassEffect(m_sampleFramebuffer, source,
                                     windowBox, transformBox, glassAlpha,
                                     cornerRadius, roundingPower, m_samplePaddingRatio, ctx);
    drawShadowIfWanted();
}

eDecorationType CGlassDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

// Driven by the real position/size variables and by map/layout/rule changes,
// unlike draw(), which sees whatever geometry the caller substituted. The
// PHLWINDOW argument is ignored on purpose: a third-party replay passes its own
// handle, while our state is keyed on the owner we were constructed with.
void CGlassDecoration::updateWindow(PHLWINDOW) {
    // A plugin may replay this mid-render under substituted geometry. mainFB is
    // set exactly between begin() and end(); currentFB also follows binds taken
    // outside a pass. Above the m_last* store, so the next real update bumps.
    if (g_pHyprRenderer->m_renderData.mainFB)
        return;

    damageEntire();

    if (!g_pGlobalState || resolveEnabled() != EEnabledResolution::Enabled)
        return;

    const auto ownWindow = m_window.lock();
    if (!ownWindow)
        return;

    const auto monitor = ownWindow->m_monitor.lock();
    if (!monitor)
        return;

    const auto currentPosition = ownWindow->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const auto currentSize     = ownWindow->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    if (currentPosition == m_lastPosition && currentSize == m_lastSize)
        return;

    m_lastPosition = currentPosition;
    m_lastSize     = currentSize;

    // A workspace the monitor is not rendering changes nothing behind a glassed
    // layer; switching to it bumps on its own. Same predicate Hyprland renders
    // by: a slide or fade keeps drawing an already-invisible workspace.
    const auto workspace = ownWindow->m_workspace;
    if (workspace && !workspace->m_visible && !workspace->m_forceRendering && !workspace->m_renderOffset->isBeingAnimated() &&
        !workspace->m_alpha->isBeingAnimated() && !ownWindow->m_pinned)
        return;

    g_pGlobalState->bumpSceneGeneration(monitor);
}

void CGlassDecoration::damageEntire() {
    const auto window = m_window.lock();
    if (!window)
        return;

    // Padded so the render pass re-renders background content (wallpaper,
    // other windows) in the sampling margin too. Without this, the scissored
    // render pass leaves stale previous-frame content in the padding area,
    // causing noise artifacts.
    const auto box = WindowGeometry::computePaddedGlobalBox(window, GlassRenderer::SAMPLE_PADDING_PX);
    if (!box)
        return;

    auto damaged = *box;
    // The overlap shadow reaches beyond the sampling padding.
    if (const int range = overlapShadowRange(); range > 0) {
        const auto  monitor = window->m_monitor.lock();
        const float scale   = monitor && monitor->m_scale > 0.0f ? monitor->m_scale : 1.0f;
        const float padding = GlassRenderer::SAMPLE_PADDING_PX / scale;
        if (range > padding)
            damaged.expand(range - padding);
    }

    g_pHyprRenderer->damageBox(damaged);
}

eDecorationLayer CGlassDecoration::getDecorationLayer() {
    return DECORATION_LAYER_BOTTOM;
}

uint64_t CGlassDecoration::getDecorationFlags() {
    return DECORATION_NON_SOLID;
}

std::string CGlassDecoration::getDisplayName() {
    return "HyprGlass";
}
