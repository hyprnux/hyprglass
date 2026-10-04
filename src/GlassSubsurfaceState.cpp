#include "GlassSubsurfaceState.hpp"
#include "Diagnostics.hpp"
#include "GlassDecoration.hpp"
#include "Globals.hpp"
#include "ItemHints.hpp"
#include "SubsurfaceGeometry.hpp"
#include "WindowGeometry.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>

// CSS border-radius overlap rule: if two radii sharing an edge would sum to
// more than that edge's length, every radius (not just that pair) shrinks by
// the smallest offending edge's own ratio, keeping all four in proportion.
static void clampRadiiToBox(std::array<float, 4>& radii, float width, float height) {
    float      ratio     = 1.0f;
    const auto shrinkFor = [&](float a, float b, float edge) {
        if (a + b > edge && a + b > 0.0f)
            ratio = std::min(ratio, edge / (a + b));
    };
    shrinkFor(radii[0], radii[1], width);  // top:    top-left + top-right
    shrinkFor(radii[1], radii[2], height); // right:  top-right + bottom-right
    shrinkFor(radii[2], radii[3], width);  // bottom: bottom-right + bottom-left
    shrinkFor(radii[3], radii[0], height); // left:   bottom-left + top-left

    if (ratio < 1.0f)
        for (float& r : radii)
            r *= ratio;
}

CGlassSubsurfaceState::CGlassSubsurfaceState(WP<CWLSurfaceResource> surface, PHLWINDOWREF window)
    : m_surface(std::move(surface)), m_window(std::move(window)) {
}

CGlassSubsurfaceState::~CGlassSubsurfaceState() = default;

bool CGlassSubsurfaceState::resolveThemeIsDark() const {
    try {
        const auto window = m_window.lock();
        if (window) {
            if (auto* deco = glassDecorationFor(window))
                return deco->isThemeDark();
        }

        const auto& config = g_pGlobalState->config;
        const auto  theme  = readStringConfig(config.defaultTheme);
        if (!theme.empty())
            return theme != "light";
    } catch (...) {}

    return true;
}

std::string CGlassSubsurfaceState::resolvePresetName(SPresetHintOutcome* hintOutcome) const {
    if (hintOutcome)
        *hintOutcome = {};

    try {
        const auto& config = g_pGlobalState->config;

        // Client hint (highest priority): only when it names a preset the
        // current config actually resolves (built-in or user-defined) — an
        // unrecognised or empty hint behaves like unset_preset, falling
        // through to the chain below (protocols/hyprglass-item-v1.xml).
        if (const auto surface = m_surface.lock()) {
            if (const auto hints = ItemHints::forSurface(surface.get())) {
                if (!hints->preset.empty()) {
                    const bool known = g_pGlobalState->customPresets.contains(hints->preset);
                    if (hintOutcome)
                        *hintOutcome = {hints->preset, !known};
                    if (known)
                        return hints->preset;
                }
            }
        }

        // subsurfaces:preset
        const auto subsurfacesPreset = readStringConfig(config.subsurfacesPreset);
        if (!subsurfacesPreset.empty())
            return std::string(subsurfacesPreset);

        // Falls back to layers:preset when set, matching the task's requested
        // default: "the layers preset if set, else the window default preset".
        const auto layersPreset = readStringConfig(config.layersPreset);
        if (!layersPreset.empty())
            return std::string(layersPreset);

        // Window default: same tag/config resolution as the window's own glass.
        const auto window = m_window.lock();
        if (window) {
            if (auto* deco = glassDecorationFor(window))
                return deco->presetName();
        }

        const auto defaultPreset = readStringConfig(config.defaultPreset);
        if (!defaultPreset.empty())
            return std::string(defaultPreset);
    } catch (...) {}

    return "default";
}

void CGlassSubsurfaceState::sampleAndRedirect(Render::CRenderContext& ctx, PHLMONITOR monitor, const CBox& transformBox, float alpha) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    shaderManager.initializeIfNeeded();

    if (!shaderManager.isInitialized() || !monitor)
        return;

    auto source = ctx.m_data.currentFB;
    if (!source)
        return;

    // Brackets the resample decision and, on a cache miss, the sample+blur
    // work below — mirrors CGlassLayerSurface::sampleAndRedirect(). This also
    // gives `hyprctl hyprglass stats` a compositor-side counter proving this
    // hook ran per item, per frame — this plugin never logs to hyprland.log,
    // so the hyprctl stats command is the only corroboration available.
    Diagnostics::CScopedStageTimer stageTimer(ctx, Diagnostics::EStage::SubsurfaceSample);
    const MONITORID monitorId = monitor ? monitor->m_id : -1; // -1 mirrors Hyprland's own MONITOR_INVALID

    const uint64_t currentGeneration = g_pGlobalState->getSceneGeneration(monitor);
    const bool     movedOrResized    = transformBox.x != m_lastTransformBox.x || transformBox.y != m_lastTransformBox.y ||
                                    transformBox.w != m_lastTransformBox.w || transformBox.h != m_lastTransformBox.h;
    m_lastTransformBox = transformBox;

    const bool backgroundChanged = !m_hasCachedSample || currentGeneration != m_lastSceneGeneration || movedOrResized;

    const bool sampleCovered = !backgroundChanged ||
        GlassRenderer::sampleRegionCovered(transformBox, source, ctx.m_data.damage, monitor);

    if (!sampleCovered) {
        // The work buffer is cleared outside this frame's damage: sampling now
        // would cache black. Defer to next frame; reuse the cached sample if we
        // have one, otherwise skip the redirect entirely (compositeAndRestore
        // bails via m_redirectedThisFrame).
        g_pHyprRenderer->damageMonitor(monitor);
        Diagnostics::recordSubsurfaceDeferredResample(monitorId);
        if (!m_hasCachedSample)
            return;
    } else if (backgroundChanged) {
        Diagnostics::recordSubsurfaceCacheMiss(monitorId);

        const bool             isDark          = resolveThemeIsDark();
        const std::string      preset          = resolvePresetName();
        const SResolveContext  resolveContext  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

        float blurStrength = resolvePresetFloat(resolveContext, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
        int   downscale    = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

        GlassRenderer::sampleBackground(ctx, m_sampleFramebuffer, source, transformBox, m_samplePaddingRatio, downscale);

        float blurRadius     = blurStrength * 12.0f / downscale;
        int   blurIterations = std::clamp(static_cast<int>(resolvePresetInt(resolveContext, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);

        if (resolveContext.config.blurFold && **resolveContext.config.blurFold) {
            const auto folded = GlassRenderer::foldBlurPasses(blurRadius, blurIterations);
            blurRadius        = folded.radius;
            blurIterations    = folded.iterations;
        }

        GlassRenderer::blurBackground(ctx, m_sampleFramebuffer, blurRadius, blurIterations, source);

        m_hasCachedSample     = true;
        m_lastSceneGeneration = currentGeneration;
    } else {
        // background unchanged, reuse cached blur
        Diagnostics::recordSubsurfaceCacheHit(monitorId);
    }

    // Redirect the item's own draw (called between this and compositeAndRestore
    // by main.cpp's CRenderPass::add hook) into a transparent temp FBO, exactly
    // like CGlassLayerSurface — its rendered alpha becomes the mask/foreground.
    int monitorWidth  = static_cast<int>(source->m_size.x);
    int monitorHeight = static_cast<int>(source->m_size.y);

    DRMFormat tempFormat = monitor->useFP16() ? source->m_drmFormat : DRM_FORMAT_ARGB8888;

    // One shared FBO per monitor for every item on it this frame (see
    // Globals.hpp), not one per item — items on a monitor are visited
    // serially (see main.cpp's hkRenderPassAdd), so redirect -> surface draw
    // -> composite -> next item never overlaps and reuse here is safe.
    auto& sharedFB = g_pGlobalState->subsurfaceTempFramebuffers[monitorId];
    if (!sharedFB)
        sharedFB = g_pHyprRenderer->createFB("hyprglass-subsurface-temp");

    if (sharedFB->m_size.x != monitorWidth || sharedFB->m_size.y != monitorHeight || sharedFB->m_drmFormat != tempFormat)
        sharedFB->alloc(monitorWidth, monitorHeight, tempFormat);

    m_savedCurrentFB = source;

    ctx.m_data.currentFB = sharedFB;
    glBindFramebuffer(GL_FRAMEBUFFER, dynamic_cast<Render::GL::CGLFramebuffer*>(sharedFB.get())->getFBID());

    CBox clearBox = transformBox.intersection(CBox{0.0, 0.0, static_cast<double>(monitorWidth), static_cast<double>(monitorHeight)}).noNegativeSize().round();

    if (std::isfinite(clearBox.x) && std::isfinite(clearBox.y) && std::isfinite(clearBox.w) && std::isfinite(clearBox.h) &&
        clearBox.w > 0.0 && clearBox.h > 0.0) {
        g_pHyprOpenGL->scissor(ctx, clearBox, false);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        g_pHyprOpenGL->scissor(ctx, nullptr);
    }

    m_redirectedThisFrame = true;
}

void CGlassSubsurfaceState::compositeAndRestore(Render::CRenderContext& ctx, PHLMONITOR monitor, const CBox& rawBox,
                                                 const CBox& transformBox, CRegion& transformedRegion, float alpha) {
    if (m_savedCurrentFB) {
        ctx.m_data.currentFB = m_savedCurrentFB;
        glBindFramebuffer(GL_FRAMEBUFFER, dynamic_cast<Render::GL::CGLFramebuffer*>(m_savedCurrentFB.get())->getFBID());
        m_savedCurrentFB.reset();
    }

    if (!m_redirectedThisFrame)
        return;
    m_redirectedThisFrame = false;

    auto& shaderManager = g_pGlobalState->shaderManager;
    if (!shaderManager.isInitialized() || !m_hasCachedSample || !monitor)
        return;

    auto target = ctx.m_data.currentFB;
    if (!target)
        return;

    // Brackets mask setup + the applyGlassEffect call below; that call's own
    // ApplyGlassEffect bracket sees this one already open and no-ops instead
    // of nesting (GL forbids concurrent GL_TIME_ELAPSED queries) — mirrors
    // CGlassLayerSurface::compositeAndRestore().
    Diagnostics::CScopedStageTimer stageTimer(ctx, Diagnostics::EStage::SubsurfaceComposite);
    Diagnostics::recordSubsurfaceGlassDraw(monitor->m_id);

    const bool            isDark          = resolveThemeIsDark();
    SPresetHintOutcome    presetHint;
    const std::string     preset          = resolvePresetName(&presetHint);
    const SResolveContext resolveContext  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    float roundingPower = 2.0f;

    auto& sharedFB    = g_pGlobalState->subsurfaceTempFramebuffers[monitor->m_id];
    int monitorWidth  = static_cast<int>(sharedFB->m_size.x);
    int monitorHeight = static_cast<int>(sharedFB->m_size.y);

    GlassRenderer::SMaskInfo maskInfo{
        .textureId = sharedFB->getTexture()->m_texID,
        .target    = GL_TEXTURE_2D,
        .uvOffset  = {transformBox.x / monitorWidth, transformBox.y / monitorHeight},
        .uvScale   = {transformBox.w / monitorWidth, transformBox.h / monitorHeight},
    };

    maskInfo.maskMode       = 1; // ext-background-effect-v1 protocol region — always, for subsurface items
    maskInfo.alphaThreshold = 0.0f;

    // Glass shape. NONE (the default, and the fallback for EXPLICIT/INHERIT_WINDOW
    // when their own geometry can't be resolved — e.g. INHERIT_WINDOW with no
    // parent window, protocols/hyprglass-item-v1.xml): a rounded box over the
    // region's own extents (which can be smaller than the item's full box —
    // see Shaders.hpp's glassBoxOffsetPx), corners from
    // plugin:hyprglass:subsurfaces:radius (-1 default = capsule, min(w,h)/2 of
    // that box; >= 0 = the exact logical px, scaled by monitor scale like
    // every other logical-px size). EXPLICIT and INHERIT_WINDOW override the
    // box and radii below with the client's own hint.
    std::array<float, 4> radii{0.0f, 0.0f, 0.0f, 0.0f};
    bool                 haveGlassBox = false;

    const auto surface = m_surface.lock();
    const auto hints    = surface ? ItemHints::forSurface(surface.get()) : std::nullopt;

    if (hints && hints->shapeMode == eItemShapeMode::EXPLICIT && hints->width > 0.0 && hints->height > 0.0) {
        // Surface-local logical -> monitor-local pixel: rawBox's own origin
        // already carries the item's on-monitor placement scaled the same
        // way (see SubsurfaceGeometry::toPixelBox), so the hint only needs
        // the same scale applied to its own offset/size.
        const float scale = static_cast<float>(monitor->m_scale);
        CBox        hintPixelBox{rawBox.x + hints->x * scale, rawBox.y + hints->y * scale, hints->width * scale, hints->height * scale};

        if (std::isfinite(hintPixelBox.x) && std::isfinite(hintPixelBox.y) && std::isfinite(hintPixelBox.w) && std::isfinite(hintPixelBox.h) &&
            hintPixelBox.w > 0.0 && hintPixelBox.h > 0.0) {
            radii = {static_cast<float>(hints->radii[0]) * scale, static_cast<float>(hints->radii[1]) * scale,
                     static_cast<float>(hints->radii[2]) * scale, static_cast<float>(hints->radii[3]) * scale};
            // Clamped against the hint's own (pre-transform) box — the pairing
            // radii[0]+radii[1] means "top edge" in the client's own frame,
            // regardless of how the monitor transform later permutes the array.
            clampRadiiToBox(radii, static_cast<float>(hintPixelBox.w), static_cast<float>(hintPixelBox.h));

            CBox transformedHintBox = WindowGeometry::applyMonitorTransform(hintPixelBox, monitor);
            transformedHintBox.noNegativeSize().round();
            maskInfo.glassBoxOffsetPx = Vector2D(transformedHintBox.x - transformBox.x, transformedHintBox.y - transformBox.y);
            maskInfo.glassBoxSizePx   = Vector2D(transformedHintBox.w, transformedHintBox.h);
            haveGlassBox              = true;
        }
    } else if (hints && hints->shapeMode == eItemShapeMode::INHERIT_WINDOW) {
        if (const auto window = m_window.lock()) {
            if (const auto windowBox = WindowGeometry::computeWindowBox(window, monitor)) {
                CBox transformedWindowBox = WindowGeometry::applyMonitorTransform(*windowBox, monitor);
                maskInfo.glassBoxOffsetPx = Vector2D(transformedWindowBox.x - transformBox.x, transformedWindowBox.y - transformBox.y);
                maskInfo.glassBoxSizePx   = Vector2D(transformedWindowBox.w, transformedWindowBox.h);
                haveGlassBox              = true;

                const float windowRadius = window->presentation().rounding() * static_cast<float>(monitor->m_scale);
                radii                    = {windowRadius, windowRadius, windowRadius, windowRadius};
                roundingPower            = window->presentation().roundingPower();
            }
        }
        // else: no parent window (e.g. a layer-shell or standalone surface) —
        // falls through to the NONE shape below, matching the protocol's
        // documented "no effect" behaviour for set_inherit_shape.
    }

    if (!haveGlassBox) {
        CBox glassExtents = transformedRegion.getExtents().intersection(transformBox);
        if (std::isfinite(glassExtents.x) && std::isfinite(glassExtents.y) && std::isfinite(glassExtents.w) &&
            std::isfinite(glassExtents.h) && glassExtents.w > 0.0 && glassExtents.h > 0.0) {
            maskInfo.glassBoxOffsetPx = Vector2D(glassExtents.x - transformBox.x, glassExtents.y - transformBox.y);
            maskInfo.glassBoxSizePx   = Vector2D(glassExtents.w, glassExtents.h);

            const auto& config           = g_pGlobalState->config;
            const float configuredRadius = config.subsurfacesRadius ? static_cast<float>(**config.subsurfacesRadius) : -1.0f;
            const float radius = configuredRadius < 0.0f ? static_cast<float>(std::min(glassExtents.w, glassExtents.h)) / 2.0f :
                                                             configuredRadius * static_cast<float>(monitor->m_scale);
            radii = {radius, radius, radius, radius};
        }
        // else: extents degenerate (shouldn't happen — the hook bails on an empty
        // region before this state is ever touched) — leave the sentinel default,
        // applyGlassEffect falls back to the whole box with every radius 0.
    }

    // Monitor rotation/mirroring permutes which physical corner each array
    // slot refers to. A no-op whenever all four radii are equal (NONE, INHERIT_WINDOW).
    radii = SubsurfaceGeometry::permuteRadiiForMonitorTransform(radii, monitor);

    const auto rects = transformedRegion.getRects();
    if (rects.size() <= static_cast<size_t>(GlassRenderer::MAX_REGION_RECTS)) {
        maskInfo.regionRectCount = static_cast<int>(rects.size());
        for (size_t i = 0; i < rects.size(); i++) {
            const auto& r           = rects[i];
            maskInfo.regionRects[i] = {static_cast<float>(r.x1 - transformBox.x), static_cast<float>(r.y1 - transformBox.y),
                                        static_cast<float>(r.x2 - r.x1), static_cast<float>(r.y2 - r.y1)};
        }
    } else {
        const auto extents       = transformedRegion.getExtents();
        maskInfo.regionRectCount = 1;
        maskInfo.regionRects[0]  = {static_cast<float>(extents.x - transformBox.x), static_cast<float>(extents.y - transformBox.y),
                                     static_cast<float>(extents.w), static_cast<float>(extents.h)};
    }

    // Sample box always equals the draw box for subsurface items (no
    // region-shrink optimization — see class comment), so the quad UV maps
    // 1:1 onto the sample texture's own normalized space.
    maskInfo.sampleUVOffset = Vector2D(0.0, 0.0);
    maskInfo.sampleUVScale  = Vector2D(1.0, 1.0);

    CBox mutableRawBox       = rawBox;
    CBox mutableTransformBox = transformBox;
    GlassRenderer::applyGlassEffect(ctx,
                                     m_sampleFramebuffer, target,
                                     mutableRawBox, mutableTransformBox, alpha,
                                     radii, roundingPower, m_samplePaddingRatio, resolveContext,
                                     &maskInfo);

    // Record the box/radii/roundingPower/preset already computed above for
    // `hyprctl hyprglass items` (Diagnostics.cpp) — a negative glassBoxSizePx
    // is the sentinel meaning "whole box" (see SMaskInfo), same fallback the
    // shader itself applies.
    m_lastGlassBox = maskInfo.glassBoxSizePx.x >= 0.0 && maskInfo.glassBoxSizePx.y >= 0.0 ?
        CBox{transformBox.x + maskInfo.glassBoxOffsetPx.x, transformBox.y + maskInfo.glassBoxOffsetPx.y,
             maskInfo.glassBoxSizePx.x, maskInfo.glassBoxSizePx.y} :
        transformBox;
    m_lastRadii          = radii;
    m_lastRoundingPower  = roundingPower;
    m_lastResolvedPreset = preset;
    m_lastPresetHint     = std::move(presetHint);
    m_lastMonitorName    = monitor->m_name;
    m_hasDrawnOnce       = true;
}
