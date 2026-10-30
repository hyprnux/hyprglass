#include "GlassDecoration.hpp"
#include "BackgroundDamageObserver.hpp"
#include "BuiltInPresets.hpp"
#include "Diagnostics.hpp"
#include "GlassPassElement.hpp"
#include "GlassRenderer.hpp"
#include "GlassSnapshotElement.hpp"
#include "Globals.hpp"
#include "Hash.hpp"
#include "RenderGuards.hpp"
#include "WindowGeometry.hpp"
#include "WorkspaceAnimation.hpp"

#include <algorithm>
#include <GLES3/gl32.h>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

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

// Not std::clamp: a NaN must read as off, not reach the shader.
static float focusLightFor(const SResolveContext& ctx) {
    const float value = resolvePresetFloat(ctx, &SPresetValues::focusLight, &SOverridableConfig::focusLight);
    return value > 0.0f ? std::min(value, 1.0f) : 0.0f;
}

// Not std::clamp: a NaN must read as off, not reach the shader.
static float frostFor(const SResolveContext& ctx) {
    const float value = resolvePresetFloat(ctx, &SPresetValues::frost, &SOverridableConfig::frost, GlobalDefaults::FROST);
    return value > 0.0f ? std::min(value, 1.0f) : 0.0f;
}

static float frostBlurFor(const SResolveContext& ctx) {
    const float value = resolvePresetFloat(ctx, &SPresetValues::frostBlur, &SOverridableConfig::frostBlur, GlobalDefaults::FROST_BLUR);
    return value > 0.0f ? std::min(value, Frost::BLUR_MAX) : 0.0f;
}

static constexpr std::chrono::milliseconds FOCUS_LIGHT_FADE{200};

// Hyprland's animations:enabled; a key that cannot be read counts as on.
static bool hyprlandAnimationsEnabled() {
    const auto& pointer = g_pGlobalState->config.animationsEnabled;
    return !pointer.dataptr || readIntegerConfig(pointer) != 0;
}

bool CGlassDecoration::focusLightFading(std::chrono::steady_clock::time_point now) const {
    return now - m_focusLightChangedAt < FOCUS_LIGHT_FADE && hyprlandAnimationsEnabled();
}

float CGlassDecoration::focusLightLevel(std::chrono::steady_clock::time_point now) const {
    const float target = m_focusLightTarget ? 1.0f : 0.0f;
    if (!focusLightFading(now))
        return target;

    const float progress = std::clamp(std::chrono::duration<float>(now - m_focusLightChangedAt) / FOCUS_LIGHT_FADE, 0.0f, 1.0f);
    const float eased    = progress * progress * (3.0f - 2.0f * progress);
    return m_focusLightFrom + (target - m_focusLightFrom) * eased;
}

// The light lies inside the glass box: damaging the window box is enough, and
// unlike damageEntire() leaves the sampling margin around it alone.
static void damageGlassBox(const PHLWINDOW& window) {
    if (!window)
        return;

    if (const auto box = WindowGeometry::computePaddedGlobalBox(window, 0.0f))
        g_pHyprRenderer->damageBox(*box);
}

void CGlassDecoration::damageFocusLight() {
    damageGlassBox(m_window.lock());
}

void CGlassDecoration::damageFrost() {
    damageGlassBox(m_window.lock());
}

void CGlassDecoration::onActiveWindowChanged(const PHLWINDOW& activeWindow) {
    // A closing last window is announced as active with focus already cleared.
    const auto focus  = Desktop::focusState();
    const bool active = activeWindow && ownsWindow(activeWindow) && focus && focus->window() == activeWindow;
    if (active == m_focusLightTarget)
        return;

    const auto now        = std::chrono::steady_clock::now();
    m_focusLightFrom      = focusLightLevel(now);
    m_focusLightTarget    = active;
    m_focusLightChangedAt = now;

    // a hidden workspace keeps its last strength; renderPass() damages it once shown
    const auto window = m_window.lock();
    if (m_lastFocusLightStrength > 0.0f && window && window->m_workspace && window->m_workspace->visible())
        damageFocusLight();
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
        if (skipOpaque && window && window->presentation().opaque()) {
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

bool CGlassDecoration::resolveXray() const {
    if (!xrayHasBackground())
        return false;

    const bool            isDark = resolveThemeIsDark();
    const std::string     preset = resolvePresetName();
    const SResolveContext ctx    = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};
    return resolvePresetInt(ctx, &SPresetValues::xray, &SOverridableConfig::xray, GlobalDefaults::XRAY) > 0;
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
    info.desiredExtents = {{0, 0}, {0, 0}};
    return info;
}

void CGlassDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {}

void CGlassDecoration::queueGlassPass(Render::CRenderContext& ctx, float alpha, bool xray) {
    // A duplicate copy is redirected into the dedupe sink and dropped whole: it
    // needs no glass, and stamping it would leave the surviving element stale.
    if (g_pGlobalState->dedupe.guard)
        return;

    CGlassPassElement::SGlassPassData data{m_self, alpha};
    data.xray = xray;

    // Only the real monitor pass is de-duplicated: snapshots, screencopy and
    // overview framebuffers render the window once, out of frame order.
    const bool managed = g_pGlobalState->frameSerial != 0 && !ctx.m_renderingSnapshot &&
        ctx.m_data.projectionType == Render::RPT_MONITOR;

    if (managed) {
        if (m_glassFrameSerial != g_pGlobalState->frameSerial) {
            m_glassFrameSerial = g_pGlobalState->frameSerial;
            m_glassQueueIndex  = 0;
        } else
            ++m_glassQueueIndex;

        data.frameSerial = m_glassFrameSerial;
        data.queueIndex  = m_glassQueueIndex;
    }

    // The pass current at RENDER_PRE_WINDOW, not the one current now: draw() runs inside
    // Hyprland's redirect for transformed windows (wobble, motion blur), whose nested
    // pass renders into a work buffer cleared to transparent, so there is nothing to
    // sample. A plugin's own redirect around renderWindow still receives it.
    const auto windowPass = g_pGlobalState->dedupe.windowPass;
    (windowPass ? *windowPass : g_pHyprRenderer->currentPass(ctx)).add(makeUnique<CGlassPassElement>(data));
}

// Render-order fingerprint: fold this window's identity/geometry/alpha into
// the monitor's running hash in z-order, before resolveEnabled() so background
// windows still count. Guarded by isForeignRender(), not a bare mainFB check,
// since a foreign replay's z-order isn't this frame's real one. Folded at most
// once per frameSerial: draw() itself runs 2-3x per monitor frame for a floating
// window over fullscreen, and folding every copy would make the hash depend on
// how many of those copies happened to render this frame rather than on the scene.
void CGlassDecoration::foldRenderFingerprint(Render::CRenderContext& ctx, const PHLMONITOR& monitor, float alpha) {
    if (!g_pGlobalState || !monitor || RenderGuards::isForeignRender(ctx))
        return;

    if (g_pGlobalState->frameSerial != 0 && m_lastFoldedFrameSerial == g_pGlobalState->frameSerial)
        return;

    const auto window = m_window.lock();
    if (!window)
        return;

    const auto workspace = window->m_workspace;
    const Vector2D workspaceRenderOffset =
        (workspace && !(window->m_state & Desktop::View::WINDOW_STATE_PINNED)) ? workspace->m_renderOffset->value() : Vector2D();
    const auto fullscreenMode = Fullscreen::controller()->getFullscreenModes(window).internal;

    auto& fingerprint = g_pGlobalState->renderFingerprints[monitor->m_id];
    Hash::hashCombine(fingerprint.runningHash, window.get(), window->positionAnimation()->value(),
                       window->sizeAnimation()->value(), alpha, fullscreenMode, workspaceRenderOffset);

    // The fade itself is not folded, only its start and its end: glass
    // stacked above resamples the settled light, not every fade step.
    m_focusLightFadingAtDraw = m_lastFocusLightStrength > 0.0f && focusLightFading(std::chrono::steady_clock::now());
    if (m_lastFocusLightStrength > 0.0f)
        Hash::hashCombine(fingerprint.runningHash, m_focusLightFadingAtDraw ? 2 : static_cast<int>(m_focusLightTarget));

    // Same for frost: glass stacked above resamples where the ease starts and where it ends.
    if (m_frost.moving() || m_frost.amount() > 0.0f)
        Hash::hashCombine(fingerprint.runningHash, m_frost.moving() ? 2 : 1);

    m_lastFoldedFrameSerial = g_pGlobalState->frameSerial;
}

void CGlassDecoration::draw(Render::CRenderContext& ctx, PHLMONITOR monitor, float const& alpha, const Render::SWindowRenderPresentation&) {
    if (!g_pGlobalState)
        return;

    foldRenderFingerprint(ctx, monitor, alpha);

    const auto enabledResolution = resolveEnabled();
    const bool enabled = enabledResolution == EEnabledResolution::Enabled;
    updateNoBlurProp(enabled);
    if (enabled && !m_glassWasEnabled)
        scheduleShadowsForWindowGlass();
    m_glassWasEnabled = enabled;
    if (!enabled) {
        m_lastSelfSample         = 0.0f;
        m_lastFocusLightStrength = 0.0f;
        m_frost.reset();

        // Only count a skip that resolveEnabled() itself attributes to
        // skip_opaque_windows — a tag or global-disable skip doesn't have a
        // counter of its own (see EEnabledResolution) and isn't counted here.
        if (monitor && enabledResolution == EEnabledResolution::DisabledBecauseOpaque)
            Diagnostics::recordWindowOpaqueSkipped(monitor->m_id);
        return;
    }

    // A foreign render must not queue a pass element at all: it would sample its own
    // framebuffer into this window's real, persistent background cache.
    if (RenderGuards::isForeignRender(ctx))
        return;

    const bool xray = resolveXray();
    if (xray)
        requestXraySnapshot(monitor);

    queueGlassPass(ctx, alpha, xray);

    // A slide translates the scene under us without any geometry change, and
    // Hyprland's per-tick window damage carries none of our sampling padding.
    // Damage only: no cache state may be touched from a render.
    const auto window = m_window.lock();
    if (window && !(window->m_state & Desktop::View::WINDOW_STATE_PINNED)) {
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
    if (fps > 0) {
        const auto interval = std::chrono::nanoseconds(1'000'000'000 / fps);
        if (now - m_lastDirtyMark < interval) {
            m_deferredResampleAt = m_lastDirtyMark + interval;
            BackgroundDamageObserver::deferResample(*m_deferredResampleAt);
            return;
        }
    }
    m_deferredResampleAt.reset();
    m_lastDirtyMark = now;

    m_backgroundDirty = true;
    // damage the full sample region: outside the committed area the framebuffer
    // still holds our previous glass output, which must not be re-sampled
    damageEntire();
}

void CGlassDecoration::flushDeferredResample(std::chrono::steady_clock::time_point now) {
    if (!m_deferredResampleAt)
        return;

    if (now < *m_deferredResampleAt) {
        BackgroundDamageObserver::deferResample(*m_deferredResampleAt);
        return;
    }

    m_deferredResampleAt.reset();
    markBackgroundDirty();
}

bool CGlassDecoration::wantsBackgroundResample(Render::CRenderContext& ctx, PHLMONITOR monitor, const CBox& windowBox, bool xray) const {
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

    if (m_cachedFromSnapshot != xray)
        return true; // x-ray switched on or off

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

    const auto source = ctx.m_data.currentFB;
    if (!m_sampleFramebuffer || !source)
        return true;

    // Monitor/scale/DPI/HDR change: recompute the size sampleBackground()
    // would allocate this frame (never cached) and compare against the FBO's
    // actual size/format from the last real sample.
    const bool isDark                     = resolveThemeIsDark();
    const std::string preset              = resolvePresetName();
    const SResolveContext resolveContext  = {preset, isDark, config, g_pGlobalState->customPresets};
    const float blurStrength              = resolvePresetFloat(resolveContext, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
    const int   downscale                 = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

    const int fullWidth  = static_cast<int>(windowBox.w) + 2 * GlassRenderer::SAMPLE_PADDING_PX;
    const int fullHeight = static_cast<int>(windowBox.h) + 2 * GlassRenderer::SAMPLE_PADDING_PX;
    const int requiredWidth  = std::max(1, fullWidth / downscale);
    const int requiredHeight = std::max(1, fullHeight / downscale);

    if (m_sampleFramebuffer->m_size.x != requiredWidth || m_sampleFramebuffer->m_size.y != requiredHeight ||
        m_sampleFramebuffer->m_drmFormat != source->m_drmFormat)
        return true;

    return false;
}

void CGlassDecoration::renderPass(Render::CRenderContext& ctx, PHLMONITOR monitor, const float& alpha, bool xray) {
    // Belt and braces: draw() already refuses to queue a pass element for a foreign
    // render, but a caller that reaches renderPass() during one anyway must not sample
    // the foreign framebuffer into this decoration's persistent background cache.
    if (RenderGuards::isForeignRender(ctx))
        return;

    auto& shaderManager = g_pGlobalState->shaderManager;
    shaderManager.initializeIfNeeded();

    if (!shaderManager.isInitialized())
        return;

    const auto window = m_window.lock();
    if (!window)
        return;

    const auto source = ctx.m_data.currentFB;
    if (!source)
        return;

    auto optBox = WindowGeometry::computeWindowBox(window, monitor);
    if (!optBox)
        return;

    if (monitor)
        Diagnostics::recordWindowGlassDraw(monitor->m_id);

    const CBox windowBox = *optBox;

    // Every non-discarded frame needs this regardless of cache hit/miss: a
    // cache-hit frame has no sampleBackground()/blurBackground() call for it
    // to sit after, and reusing a stale value here (e.g. glassAlpha after a
    // fade, or cornerRadius after a fullscreen toggle) would silently
    // mis-render the composite even though the cached sample itself is fine.
    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext resolveContext  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    float monitorScale = monitor->m_scale;

    // Hyprland renders internal-fullscreen windows unrounded (dontRound), we need to
    // match, or the glass would show rounded gaps at the screen corners
    const bool fsUnrounded = Fullscreen::controller()->getFullscreenModes(window).internal == Fullscreen::FSMODE_FULLSCREEN;
    float cornerRadius  = fsUnrounded ? 0.0f : window->presentation().rounding() * monitorScale;
    float roundingPower = window->presentation().roundingPower();

    // Resolved here, not inside the cache branch below, so it stays fresh on a
    // cache-hit frame too, when neither sampleBackground() nor blendOwnContent() run.
    m_lastSelfSample = selfSampleFor(resolveContext);
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
    float glassAlpha = window->presentation().alpha().getTotalWithout(Desktop::View::WINDOW_ALPHA_ACTIVE);
    if (const auto workspace = window->m_workspace; workspace && !(window->m_state & Desktop::View::WINDOW_STATE_PINNED))
        glassAlpha *= workspace->m_alpha->value();

    // A fullscreen window shows no edge, like Hyprland draws it no border.
    m_lastFocusLightStrength = fsUnrounded ? 0.0f : focusLightFor(resolveContext);
    float focusLight         = 0.0f;
    if (m_lastFocusLightStrength > 0.0f) {
        const auto now = std::chrono::steady_clock::now();
        // window.active normally got here first; this catches the window's first
        // frame (and any missed change) without a fade
        const auto focus  = Desktop::focusState();
        const bool active = focus && focus->window() == window;
        if (active != m_focusLightTarget) {
            m_focusLightTarget    = active;
            m_focusLightFrom      = active ? 1.0f : 0.0f;
            m_focusLightChangedAt = {};
        }
        focusLight = m_lastFocusLightStrength * focusLightLevel(now);
        // damage, not a loop: it stops once draw() has seen the fade settle
        if (focusLightFading(now) || m_focusLightFadingAtDraw)
            damageFocusLight();
    }

    const MONITORID monitorId = monitor ? monitor->m_id : -1; // -1 mirrors Hyprland's own MONITOR_INVALID

    if (!wantsBackgroundResample(ctx, monitor, windowBox, xray)) {
        // Background unchanged since the last real sample — reuse it, skip
        // the most expensive GPU work (blit + blur passes) entirely.
        Diagnostics::recordWindowCacheHit(monitorId);
    } else {
        // X-ray never samples the live frame: it holds the windows it hides.
        const auto snapshot = xray ? xraySnapshotCovering(monitor, source, windowBox) : nullptr;
        const bool covered  = xray ? static_cast<bool>(snapshot) :
                                     GlassRenderer::sampleRegionCovered(windowBox, source, ctx.m_data.damage);

        if (covered) {
            float blurStrength   = resolvePresetFloat(resolveContext, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
            int downscale        = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

            GlassRenderer::sampleBackground(ctx, m_sampleFramebuffer, xray ? snapshot : source, windowBox, m_samplePaddingRatio, downscale);

            // Only here, on a real (non-cached) sample: blending onto a cache-hit
            // frame would double-composite our own content over an already-blurred FBO.
            if (m_lastSelfSample > 0.0f)
                GlassRenderer::blendOwnContent(ctx, m_sampleFramebuffer, window, monitor, windowBox,
                                               downscale, m_lastSelfSample, cornerRadius, roundingPower);

            float blurRadius     = blurStrength * 12.0f / downscale;
            int blurIterations   = std::clamp(static_cast<int>(resolvePresetInt(resolveContext, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);

            if (resolveContext.config.blurFold && **resolveContext.config.blurFold) {
                const GlassRenderer::SFoldedBlur folded = GlassRenderer::foldBlurPasses(blurRadius, blurIterations);
                blurRadius     = folded.radius;
                blurIterations = folded.iterations;
            }

            GlassRenderer::blurBackground(ctx, m_sampleFramebuffer, blurRadius, blurIterations, source);

            m_frost.sampleChanged();

            m_hasCachedSample       = true;
            m_cachedFromSnapshot    = xray;
            m_lastSceneGeneration   = g_pGlobalState->getSceneGeneration(monitor);
            m_lastGenerationMonitor = monitorId;
            m_backgroundDirty       = false;
            Diagnostics::recordWindowCacheMiss(monitorId);
        } else if (m_hasCachedSample && (!xray || m_cachedFromSnapshot)) {
            // Not enough of the padded box is damaged yet to safely re-sample
            // (would pick up stale pixels outside this frame's damage), or not
            // yet in the x-ray snapshot. Force it into next frame's damage and
            // draw the stale cache for now — a future pass scissors the draw
            // to what's actually damaged.
            damageEntire();
            Diagnostics::recordWindowDeferredResample(monitorId);
            if (xray)
                Diagnostics::recordXrayDeferredResample(monitorId);
        } else {
            // No cache yet (or only a live one under x-ray) and not enough
            // damage to sample cleanly: nothing valid to draw this frame.
            damageEntire();
            if (xray)
                Diagnostics::recordXrayDeferredResample(monitorId);
            return;
        }
    }

    // Frost: blurred further where windows lie behind, clear over the wallpaper. X-ray
    // shows no window behind the glass, and a fullscreen window has nothing behind it.
    GlassRenderer::SFrostInput frostInput;
    const float                frostSetting = (xray || fsUnrounded) ? 0.0f : frostFor(resolveContext);
    if (frostSetting > 0.0f || m_frost.active()) {
        const float target =
            frostSetting > 0.0f ? frostSetting * Frost::amountForCoverage(Frost::coveredFraction(window, monitor, windowBox)) : 0.0f;

        // A window spanning two monitors gets a different target on each: it snaps on a
        // monitor change, or it would ease between them and never settle.
        const bool sameMonitor = m_frostMonitor == monitorId;
        m_frostMonitor         = monitorId;

        // damage, not a loop: it stops once the amount settles. The sample is unchanged,
        // so no resample is owed and the cache stays valid.
        if (m_frost.step(target, sameMonitor && hyprlandAnimationsEnabled(), std::chrono::steady_clock::now()))
            damageFrost();

        if (m_frost.amount() > Frost::MINIMUM_AMOUNT) {
            const float blurStrength = resolvePresetFloat(resolveContext, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
            const int   downscale    = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;
            // the copy is half the sample's size on top of the sample's own downscale
            const float radius = frostBlurFor(resolveContext) * 12.0f / (2.0f * downscale);

            if (m_frost.needsBuild(m_sampleFramebuffer, radius)) {
                float frostRadius     = radius;
                int   frostIterations = Frost::BLUR_ITERATIONS;
                if (resolveContext.config.blurFold && **resolveContext.config.blurFold) {
                    const GlassRenderer::SFoldedBlur folded = GlassRenderer::foldBlurPasses(frostRadius, frostIterations);
                    frostRadius     = folded.radius;
                    frostIterations = folded.iterations;
                }

                if (GlassRenderer::buildFrostCopy(ctx, m_sampleFramebuffer, m_frost.copy(), m_frost.temporary(), frostRadius, frostIterations, source)) {
                    m_frost.built(radius);
                    Diagnostics::recordFrostRebuild(monitorId);
                }
            }

            if (!m_frost.needsBuild(m_sampleFramebuffer, radius))
                frostInput = {m_frost.copy(), m_frost.amount()};
        } else {
            m_frost.releaseCopy();
        }
    }

    GlassRenderer::applyGlassEffect(ctx, m_sampleFramebuffer, source,
                                     windowBox, glassAlpha,
                                     std::array<float, 4>{cornerRadius, cornerRadius, cornerRadius, cornerRadius},
                                     roundingPower, m_samplePaddingRatio, resolveContext, nullptr, focusLight,
                                     frostInput.copy ? &frostInput : nullptr);
}

eDecorationType CGlassDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

// Driven by the real position/size variables and by map/layout/rule changes,
// unlike draw(), which sees whatever geometry the caller substituted. The
// PHLWINDOW argument is ignored on purpose: a third-party replay passes its own
// handle, while our state is keyed on the owner we were constructed with.
void CGlassDecoration::updateWindow(PHLWINDOW) {
    // A plugin may replay this mid-render under substituted geometry. The render
    // context is active exactly between begin() and finishRender(). Above the
    // m_last* store, so the next real update bumps.
    if (g_pHyprRenderer->context().active())
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
    if (workspace && !workspace->visible() && !workspace->m_forceRendering && !workspace->m_renderOffset->isBeingAnimated() &&
        !workspace->m_alpha->isBeingAnimated() && !(ownWindow->m_state & Desktop::View::WINDOW_STATE_PINNED))
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

    g_pHyprRenderer->damageBox(*box);
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
