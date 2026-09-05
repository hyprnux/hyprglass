#include "GlassLayerSurface.hpp"
#include "BuiltInPresets.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "LayerGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <hyprland/src/desktop/Workspace.hpp>
#include <GLES3/gl32.h>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Misc.hpp>

static CBox transformedLayerBox(CBox pixelBox, PHLMONITOR monitor) {
    const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform));
    pixelBox.transform(transform, monitor->m_transformedSize.x, monitor->m_transformedSize.y).noNegativeSize().round();
    return pixelBox;
}

CGlassLayerSurface::CGlassLayerSurface(PHLLS layerSurface)
    : m_layerSurface(layerSurface) {
}

CGlassLayerSurface::~CGlassLayerSurface() {
    // Damage the area where glass was last drawn so the compositor
    // re-renders it without the glass effect (prevents ghost artifacts).
    if (g_pHyprRenderer && m_lastSize.x > 0 && m_lastSize.y > 0 &&
        std::isfinite(m_lastPosition.x) && std::isfinite(m_lastPosition.y) &&
        std::isfinite(m_lastSize.x) && std::isfinite(m_lastSize.y)) {
        auto box = CBox{m_lastPosition, m_lastSize};
        box.expand(GlassRenderer::SAMPLE_PADDING_PX).noNegativeSize();
        if (box.w > 0.0 && box.h > 0.0)
            g_pHyprRenderer->damageBox(box);
    }
}

bool CGlassLayerSurface::resolveThemeIsDark() const {
    try {
        const auto& config = g_pGlobalState->config;
        const auto theme = readStringConfig(config.defaultTheme);
        if (!theme.empty())
            return theme != "light";
    } catch (...) {}

    return true;
}

bool CGlassLayerSurface::resolveXray() const {
    try {
        // Per-namespace setting wins over the global, either way
        const auto layerSurface = m_layerSurface.lock();
        if (layerSurface) {
            const auto& nsXray = g_pGlobalState->layerNamespaceXray;
            const auto  it     = nsXray.find(layerSurface->m_namespace);
            if (it != nsXray.end())
                return it->second;
        }
    } catch (...) {}

    const auto& config = g_pGlobalState->config;
    return config.xray && **config.xray;
}

std::string CGlassLayerSurface::resolvePresetName() const {
    try {
        // Per-namespace preset override (highest priority)
        const auto layerSurface = m_layerSurface.lock();
        if (layerSurface) {
            const auto& nsPresets = g_pGlobalState->layerNamespacePresets;
            auto it = nsPresets.find(layerSurface->m_namespace);
            if (it != nsPresets.end())
                return it->second;
        }

        const auto& config = g_pGlobalState->config;

        // Layer-wide preset override
        const auto layerPreset = readStringConfig(config.layersPreset);
        if (!layerPreset.empty())
            return std::string(layerPreset);

        // Fall back to global default preset
        const auto defaultPreset = readStringConfig(config.defaultPreset);
        if (!defaultPreset.empty())
            return std::string(defaultPreset);
    } catch (...) {}

    return "default";
}

ELayerMaskMode CGlassLayerSurface::resolveMaskMode() const {
    if (const auto layerSurface = m_layerSurface.lock()) {
        const auto& overrides = g_pGlobalState->layerNamespaceMaskModes;
        if (auto it = overrides.find(layerSurface->m_namespace); it != overrides.end())
            return it->second;
    }

    if (auto mode = parseLayerMaskMode(readStringConfig(g_pGlobalState->config.layersMaskMode)))
        return *mode;
    return ELayerMaskMode::AUTO;
}

CGlassLayerSurface::EMaskSource CGlassLayerSurface::resolveMaskSource() const {
    const auto layerSurface = m_layerSurface.lock();
    const auto wlSurface    = layerSurface ? layerSurface->wlSurface() : nullptr; // root surface only
    const bool hasEffect    = wlSurface && wlSurface->m_hasBackgroundEffect;
    const bool regionEmpty  = !wlSurface || wlSurface->m_blurRegion.empty();

    switch (resolveMaskMode()) {
        case ELayerMaskMode::ALPHA:
            return EMaskSource::ALPHA_THRESHOLD;
        case ELayerMaskMode::REGION:
            return (hasEffect && !regionEmpty) ? EMaskSource::PROTOCOL_REGION : EMaskSource::NONE;
        case ELayerMaskMode::AUTO:
        default:
            if (hasEffect)
                return regionEmpty ? EMaskSource::NONE : EMaskSource::PROTOCOL_REGION;
            return EMaskSource::ALPHA_THRESHOLD;
    }
}

bool CGlassLayerSurface::liveResampleEnabled() const {
    if (const auto layerSurface = m_layerSurface.lock()) {
        const auto& overrides = g_pGlobalState->layerNamespaceLiveResample;
        if (auto it = overrides.find(layerSurface->m_namespace); it != overrides.end())
            return it->second;
    }

    const auto& config = g_pGlobalState->config;
    return config.layersLiveResample && **config.layersLiveResample;
}

PHLLS CGlassLayerSurface::getLayerSurface() const {
    return m_layerSurface.lock();
}

void CGlassLayerSurface::damageIfMoved() {
    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    const auto currentPosition = layerSurface->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    const auto currentSize     = layerSurface->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);
    if (currentSize.x <= 0.0 || currentSize.y <= 0.0 ||
        !std::isfinite(currentPosition.x) || !std::isfinite(currentPosition.y) ||
        !std::isfinite(currentSize.x) || !std::isfinite(currentSize.y))
        return;

    const bool isAnimating = layerSurface->positionAnimation()->isBeingAnimated() ||
                             layerSurface->sizeAnimation()->isBeingAnimated() ||
                             layerSurface->alpha()[Desktop::View::LS_ALPHA_FADE]->isBeingAnimated() ||
                             !layerSurface->m_mapped;

    const bool moved = currentPosition != m_lastPosition || currentSize != m_lastSize;

    if (moved || isAnimating) {
        m_lastPosition  = currentPosition;
        m_lastSize      = currentSize;

        damageSampleRegion();

        if (const auto monitor = layerSurface->m_monitor.lock())
            g_pGlobalState->bumpSceneGeneration(monitor);
    } else if (const auto& config = g_pGlobalState->config;
               config.layersForceLiveResample && **config.layersForceLiveResample) {
        // keep frames flowing so the forced per-frame resample actually runs
        damageSampleRegion();
    }
}

void CGlassLayerSurface::damageSampleRegion() {
    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    const auto monitor = layerSurface->m_monitor.lock();
    const float scale = monitor ? monitor->m_scale : 1.0f;
    auto box = CBox{layerSurface->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT),
                    layerSurface->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT)};
    box.expand(GlassRenderer::SAMPLE_PADDING_PX / scale).noNegativeSize();
    if (box.w > 0.0 && box.h > 0.0 &&
        std::isfinite(box.x) && std::isfinite(box.y) && std::isfinite(box.w) && std::isfinite(box.h))
        g_pHyprRenderer->damageBox(box);
}

void CGlassLayerSurface::markBackgroundDirty() {
    if (m_backgroundDirty)
        return;

    const auto& config = g_pGlobalState->config;
    const int64_t fps = config.layersLiveResampleFps ? **config.layersLiveResampleFps : 0;
    const auto now = std::chrono::steady_clock::now();
    if (fps > 0 && now - m_lastDirtyMark < std::chrono::nanoseconds(1'000'000'000 / fps))
        return;
    m_lastDirtyMark = now;

    m_backgroundDirty = true;
    // damage the full sample region: outside the committed area the framebuffer
    // still holds our previous glass output, which must not be re-sampled
    damageSampleRegion();
}

void CGlassLayerSurface::sampleAndRedirect(PHLMONITOR monitor, float alpha) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    shaderManager.initializeIfNeeded();

    if (!shaderManager.isInitialized())
        return;

    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    auto source = g_pHyprRenderer->m_renderData.currentFB;
    if (!source)
        return;

    // X-ray: sample the pre-window snapshot (wallpaper only) once it is
    // complete, and keep asking for it every frame so it stays fresh. A
    // sample taken from the live frame while the snapshot is still filling
    // in is cached like any other and replaced once, when the snapshot is
    // there (or when x-ray is switched off again), so the cached-sample
    // path costs the same as without x-ray.
    auto       sampleSource = source;
    const bool xray         = resolveXray();
    if (xray && monitor) {
        auto& snapshot          = g_pGlobalState->backgroundSnapshots[monitor->m_id];
        snapshot.requestedFrame = snapshot.frame;
        if (snapshot.complete && snapshot.fb && snapshot.fb->m_size == source->m_size)
            sampleSource = snapshot.fb;
    }
    const bool fromSnapshot = sampleSource != source;

    auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!layerBox)
        return;

    CBox transformBox = transformedLayerBox(*layerBox, monitor);

    // Decide whether we need to re-sample and re-blur the background.
    // When only the layer surface content changed (e.g. waybar clock tick)
    // but no window moved behind us, we reuse the cached blurred background.
    // This skips the most expensive GPU work (blit + 6 blur passes).
    const uint64_t currentGeneration = g_pGlobalState->getSceneGeneration(monitor);
    const auto activeWs = monitor->m_activeWorkspace;
    const bool isAnimating = layerSurface->positionAnimation()->isBeingAnimated() ||
                             layerSurface->sizeAnimation()->isBeingAnimated() ||
                             layerSurface->alpha()[Desktop::View::LS_ALPHA_FADE]->isBeingAnimated() ||
                             (activeWs && activeWs->m_renderOffset->isBeingAnimated());
    const auto& config = g_pGlobalState->config;
    const bool forceLive = config.layersForceLiveResample && **config.layersForceLiveResample;
    const bool backgroundChanged = !m_hasCachedSample ||
                                   currentGeneration != m_lastSceneGeneration ||
                                   isAnimating || m_backgroundDirty || forceLive ||
                                   fromSnapshot != m_cachedFromSnapshot;

    if (!layerSurface->m_mapped) {
        // During fade-out, re-sampling captures stale pixels. Reuse cached sample.
        if (!m_hasCachedSample)
            return;
    } else if (backgroundChanged) {
        const bool isDark          = resolveThemeIsDark();
        const std::string preset   = resolvePresetName();
        const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

        float blurStrength   = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
        int downscale        = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

        GlassRenderer::sampleBackground(m_sampleFramebuffer, sampleSource, transformBox, m_samplePaddingRatio, downscale);

        float blurRadius     = blurStrength * 12.0f / downscale;
        int blurIterations   = std::clamp(static_cast<int>(resolvePresetInt(ctx, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);
        GlassRenderer::blurBackground(m_sampleFramebuffer, blurRadius, blurIterations, source);

        m_hasCachedSample      = true;
        m_cachedFromSnapshot   = fromSnapshot;
        m_lastSceneGeneration  = currentGeneration;
        m_backgroundDirty      = false;
    }
    // else: background unchanged, reuse cached blur — skip 7 GPU operations

    // Redirect surface rendering to a temp FBO cleared to transparent.
    // The original renderLayer (called between pre/post elements) will render
    // the surface into this FBO. compositeAndRestore uses its alpha as a mask.
    // Size from the source FB, not the monitor: m_transformedSize is swapped
    // relative to the framebuffer's native orientation on 90°/270° monitors (#41).
    int monitorWidth  = static_cast<int>(source->m_size.x);
    int monitorHeight = static_cast<int>(source->m_size.y);

    // In FP16/HDR mode, the source FB uses RGBA16F which has full alpha precision.
    // Use the source format to avoid clipping HDR color values.
    // In SDR mode, force ARGB8888 because monitor FBOs (XRGB2101010 etc.) have
    // limited/no alpha, which would quantize mask values and break the discard.
    DRMFormat tempFormat = (monitor->useFP16()) ? source->m_drmFormat : DRM_FORMAT_ARGB8888;

    if (!m_surfaceTempFramebuffer)
        m_surfaceTempFramebuffer = g_pHyprRenderer->createFB("hyprglass-layer-temp");

    if (m_surfaceTempFramebuffer->m_size.x != monitorWidth || m_surfaceTempFramebuffer->m_size.y != monitorHeight ||
        m_surfaceTempFramebuffer->m_drmFormat != tempFormat)
        m_surfaceTempFramebuffer->alloc(monitorWidth, monitorHeight, tempFormat);

    m_savedCurrentFB = source;

    g_pHyprRenderer->m_renderData.currentFB = m_surfaceTempFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, dynamic_cast<Render::GL::CGLFramebuffer*>(m_surfaceTempFramebuffer.get())->getFBID());

    CBox clearBox = transformBox;
    clearBox.expand(GlassRenderer::SAMPLE_PADDING_PX);
    clearBox = clearBox.intersection(CBox{0.0, 0.0, static_cast<double>(monitorWidth), static_cast<double>(monitorHeight)}).noNegativeSize().round();

    if (std::isfinite(clearBox.x) && std::isfinite(clearBox.y) && std::isfinite(clearBox.w) && std::isfinite(clearBox.h) &&
        clearBox.w > 0.0 && clearBox.h > 0.0) {
        g_pHyprOpenGL->scissor(clearBox, false);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        g_pHyprOpenGL->scissor(nullptr);
    }
}

void CGlassLayerSurface::compositeAndRestore(PHLMONITOR monitor, float alpha, EMaskSource maskSource) {
    // Restore the original currentFB before compositing
    if (m_savedCurrentFB) {
        g_pHyprRenderer->m_renderData.currentFB = m_savedCurrentFB;
        glBindFramebuffer(GL_FRAMEBUFFER, dynamic_cast<Render::GL::CGLFramebuffer*>(m_savedCurrentFB.get())->getFBID());
        m_savedCurrentFB.reset();
    }

    auto& shaderManager = g_pGlobalState->shaderManager;
    if (!shaderManager.isInitialized() || !m_hasCachedSample)
        return;

    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    auto target = g_pHyprRenderer->m_renderData.currentFB;
    if (!target)
        return;

    auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!layerBox)
        return;

    CBox rawBox       = *layerBox;
    CBox transformBox = transformedLayerBox(rawBox, monitor);

    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    float cornerRadius  = 0.0f;
    float roundingPower = 2.0f;

    // Use the temp FBO's rendered alpha as a mask: glass only where the surface
    // has visible content (alpha > 0). The temp FBO is in monitor coordinates,
    // so we map from the glass quad UV to monitor UV.
    int monitorWidth  = static_cast<int>(m_surfaceTempFramebuffer->m_size.x);
    int monitorHeight = static_cast<int>(m_surfaceTempFramebuffer->m_size.y);

    GlassRenderer::SMaskInfo maskInfo{
        .textureId = m_surfaceTempFramebuffer->getTexture()->m_texID,
        .target    = GL_TEXTURE_2D,
        .uvOffset  = {transformBox.x / monitorWidth, transformBox.y / monitorHeight},
        .uvScale   = {transformBox.w / monitorWidth, transformBox.h / monitorHeight},
    };

    switch (maskSource) {
        case EMaskSource::ALPHA_THRESHOLD: {
            float maskThreshold = 0.001f;
            auto threshIt = g_pGlobalState->layerNamespaceMaskThresholds.find(layerSurface->m_namespace);
            if (threshIt != g_pGlobalState->layerNamespaceMaskThresholds.end())
                maskThreshold = threshIt->second;

            // The temp FBO stores the layer after Hyprland applies fade alpha. Keep
            // mask_threshold relative to the layer's content alpha, otherwise fade-out
            // makes the mask fall below threshold early and the glass blinks off.
            maskInfo.maskMode       = 0;
            maskInfo.alphaThreshold = maskThreshold * std::clamp(alpha, 0.0f, 1.0f);
            break;
        }
        case EMaskSource::PROTOCOL_REGION: {
            maskInfo.maskMode       = 1;
            maskInfo.alphaThreshold = 0.0f; // unused in region mode

            const auto wlSurface = layerSurface->wlSurface();
            if (!wlSurface)
                break;
            const auto logicalSize = layerSurface->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT);

            // surface-local logical region -> box-local pixels, same
            // scale/translate/transform sequence as transformedLayerBox()
            CRegion region = wlSurface->m_blurRegion.copy();
            region.intersect(0, 0, logicalSize.x, logicalSize.y); // spec: clipped to surface size
            region.scale(static_cast<float>(monitor->m_scale));
            region.translate(rawBox.pos());
            region.intersect(rawBox.x, rawBox.y, rawBox.w, rawBox.h); // defensive
            region.transform(Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform)),
                              monitor->m_transformedSize.x, monitor->m_transformedSize.y);

            const auto rects = region.getRects();
            if (rects.size() <= static_cast<size_t>(GlassRenderer::MAX_REGION_RECTS)) {
                maskInfo.regionRectCount = static_cast<int>(rects.size());
                for (size_t i = 0; i < rects.size(); i++) {
                    const auto& r = rects[i];
                    maskInfo.regionRects[i] = {static_cast<float>(r.x1 - transformBox.x), static_cast<float>(r.y1 - transformBox.y),
                                                static_cast<float>(r.x2 - r.x1), static_cast<float>(r.y2 - r.y1)};
                }
            } else {
                // Overflow: one bounding rect rather than dropping rects (a hole
                // reads as more broken than a few extra glassed pixels at concave corners).
                const auto extents = region.getExtents();
                maskInfo.regionRectCount = 1;
                maskInfo.regionRects[0]  = {static_cast<float>(extents.x - transformBox.x), static_cast<float>(extents.y - transformBox.y),
                                             static_cast<float>(extents.w), static_cast<float>(extents.h)};
            }
            break;
        }
        case EMaskSource::NONE:
            // hkRenderLayer takes the plain-renderLayer path for NONE; never reaches here.
            break;
    }

    // The glass shader composites both the glass effect and the surface content
    // in a single pass: glass behind, surface on top, using the temp FBO alpha.
    GlassRenderer::applyGlassEffect(m_sampleFramebuffer, target,
                                     rawBox, transformBox, alpha,
                                     cornerRadius, roundingPower, m_samplePaddingRatio, ctx,
                                     &maskInfo);
}
