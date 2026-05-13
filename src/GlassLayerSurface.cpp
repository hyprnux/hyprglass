#include "GlassLayerSurface.hpp"
#include "BuiltInPresets.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "LayerGeometry.hpp"

#include <algorithm>
#include <hyprland/src/desktop/Workspace.hpp>
#include <GLES3/gl32.h>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Misc.hpp>

using Render::GL::CGLFramebuffer;
using Render::GL::g_pHyprOpenGL;

CGlassLayerSurface::CGlassLayerSurface(PHLLS layerSurface)
    : m_layerSurface(layerSurface) {
}

CGlassLayerSurface::~CGlassLayerSurface() {
    // Damage the area where glass was last drawn so the compositor
    // re-renders it without the glass effect (prevents ghost artifacts).
    if (g_pHyprRenderer && m_lastSize.x > 0 && m_lastSize.y > 0) {
        auto box = CBox{m_lastPosition, m_lastSize};
        box.expand(GlassRenderer::SAMPLE_PADDING_PX);
        g_pHyprRenderer->damageBox(box);
    }
}

bool CGlassLayerSurface::resolveThemeIsDark() const {
    try {
        const auto& config = g_pGlobalState->config;
        if (config.defaultTheme) {
            const char* theme = *config.defaultTheme;
            if (theme)
                return std::string_view(theme) != "light";
        }
    } catch (...) {}

    return true;
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
        if (config.layersPreset) {
            const char* preset = *config.layersPreset;
            if (preset && preset[0] != '\0')
                return std::string(preset);
        }

        // Fall back to global default preset
        if (config.defaultPreset) {
            const char* preset = *config.defaultPreset;
            if (preset && preset[0] != '\0')
                return std::string(preset);
        }
    } catch (...) {}

    return "default";
}

PHLLS CGlassLayerSurface::getLayerSurface() const {
    return m_layerSurface.lock();
}

void CGlassLayerSurface::damageIfMoved() {
    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    const auto currentPosition = layerSurface->m_realPosition->value();
    const auto currentSize     = layerSurface->m_realSize->value();

    const bool isAnimating = layerSurface->m_realPosition->isBeingAnimated() ||
                             layerSurface->m_realSize->isBeingAnimated() ||
                             layerSurface->m_alpha->isBeingAnimated() ||
                             layerSurface->m_fadingOut;

    const bool moved = currentPosition != m_lastPosition || currentSize != m_lastSize;

    if (moved || isAnimating) {
        m_lastPosition  = currentPosition;
        m_lastSize      = currentSize;

        auto box = CBox{currentPosition, currentSize};
        const auto monitor = layerSurface->m_monitor.lock();
        const float scale = monitor ? monitor->m_scale : 1.0f;
        box.expand(GlassRenderer::SAMPLE_PADDING_PX / scale);
        g_pHyprRenderer->damageBox(box);

        if (monitor)
            g_pGlobalState->bumpSceneGeneration(monitor.get());
    }
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

    auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!layerBox)
        return;

    CBox transformBox = *layerBox;

    const auto transform = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprRenderer->m_renderData.pMonitor->m_transform));
    transformBox.transform(transform,
        g_pHyprRenderer->m_renderData.pMonitor->m_transformedSize.x,
        g_pHyprRenderer->m_renderData.pMonitor->m_transformedSize.y);

    // Decide whether we need to re-sample and re-blur the background.
    // When only the layer surface content changed (e.g. waybar clock tick)
    // but no window moved behind us, we reuse the cached blurred background.
    // This skips the most expensive GPU work (blit + 6 blur passes).
    const uint64_t currentGeneration = g_pGlobalState->getSceneGeneration(monitor.get());
    const auto activeWs = monitor->m_activeWorkspace;
    const bool isAnimating = layerSurface->m_realPosition->isBeingAnimated() ||
                             layerSurface->m_realSize->isBeingAnimated() ||
                             layerSurface->m_alpha->isBeingAnimated() ||
                             (activeWs && activeWs->m_renderOffset->isBeingAnimated());
    const bool backgroundChanged = !m_hasCachedSample ||
                                   currentGeneration != m_lastSceneGeneration ||
                                   isAnimating;

    if (layerSurface->m_fadingOut) {
        // During fade-out, re-sampling captures stale pixels. Reuse cached sample.
        if (!m_hasCachedSample)
            return;
    } else if (backgroundChanged) {
        const bool isDark          = resolveThemeIsDark();
        const std::string preset   = resolvePresetName();
        const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

        float blurStrength   = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
        int downscale        = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

        GlassRenderer::sampleBackground(m_sampleFramebuffer, source, transformBox, m_samplePaddingRatio, downscale);

        float blurRadius     = blurStrength * 12.0f / downscale;
        int blurIterations   = std::clamp(static_cast<int>(resolvePresetInt(ctx, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);
        int viewportWidth    = static_cast<int>(g_pHyprRenderer->m_renderData.pMonitor->m_transformedSize.x);
        int viewportHeight   = static_cast<int>(g_pHyprRenderer->m_renderData.pMonitor->m_transformedSize.y);
        GlassRenderer::blurBackground(m_sampleFramebuffer, blurRadius, blurIterations, GLFB(source)->getFBID(), viewportWidth, viewportHeight);

        m_hasCachedSample      = true;
        m_lastSceneGeneration  = currentGeneration;
    }
    // else: background unchanged, reuse cached blur — skip 7 GPU operations

    // Redirect surface rendering to a temp FBO cleared to transparent.
    // The original renderLayer (called between pre/post elements) will render
    // the surface into this FBO. compositeAndRestore uses its alpha as a mask.
    int monitorWidth  = static_cast<int>(monitor->m_transformedSize.x);
    int monitorHeight = static_cast<int>(monitor->m_transformedSize.y);

    // Force ARGB8888 for the temp FBO: the mask shader needs alpha precision.
    // Monitor FBOs use XRGB formats (no usable alpha); XRGB2101010 (10-bit)
    // has only 2-bit alpha, quantizing values below ~0.17 to zero and breaking
    // the mask discard for low-opacity layers.
    if (!m_surfaceTempFramebuffer)
        m_surfaceTempFramebuffer = g_pHyprRenderer->createFB("hyprglass-layer-temp");
    if (m_surfaceTempFramebuffer->m_size.x != monitorWidth || m_surfaceTempFramebuffer->m_size.y != monitorHeight)
        m_surfaceTempFramebuffer->alloc(monitorWidth, monitorHeight, DRM_FORMAT_ARGB8888);

    m_savedCurrentFB = source;

    g_pHyprRenderer->m_renderData.currentFB = m_surfaceTempFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, GLFB(m_surfaceTempFramebuffer)->getFBID());

    // Scissored clear: only clear the layer's area + margin in the temp FBO.
    // The mask shader only reads within the layer's UV bounds, so content outside
    // doesn't matter. This avoids clearing millions of unused pixels on
    // monitor-sized FBOs (e.g. 48x48 layer on a 2560x1440 FBO).
    {
        const int pad = GlassRenderer::SAMPLE_PADDING_PX;
        const int sx  = std::max(0, static_cast<int>(transformBox.x) - pad);
        const int sy  = std::max(0, static_cast<int>(transformBox.y) - pad);
        const int sw  = std::min(monitorWidth  - sx, static_cast<int>(transformBox.w) + 2 * pad);
        const int sh  = std::min(monitorHeight - sy, static_cast<int>(transformBox.h) + 2 * pad);
        glEnable(GL_SCISSOR_TEST);
        glScissor(sx, sy, sw, sh);
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);
    }
}

void CGlassLayerSurface::compositeAndRestore(PHLMONITOR monitor, float alpha) {
    // Restore the original currentFB before compositing
    if (m_savedCurrentFB) {
        g_pHyprRenderer->m_renderData.currentFB = m_savedCurrentFB;
        glBindFramebuffer(GL_FRAMEBUFFER, GLFB(m_savedCurrentFB)->getFBID());
        m_savedCurrentFB = nullptr;
    }

    auto& shaderManager = g_pGlobalState->shaderManager;
    if (!shaderManager.isInitialized() || !m_hasCachedSample)
        return;

    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    auto target = g_pHyprRenderer->m_renderData.currentFB;
    if (!target || !m_surfaceTempFramebuffer)
        return;

    auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!layerBox)
        return;

    CBox rawBox       = *layerBox;
    CBox transformBox = rawBox;

    const auto transform = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprRenderer->m_renderData.pMonitor->m_transform));
    transformBox.transform(transform,
        g_pHyprRenderer->m_renderData.pMonitor->m_transformedSize.x,
        g_pHyprRenderer->m_renderData.pMonitor->m_transformedSize.y);

    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    float cornerRadius  = 0.0f;
    float roundingPower = 2.0f;

    // Use the temp FBO's rendered alpha as a mask: glass only where the surface
    // has visible content (alpha > 0). The temp FBO is in monitor coordinates,
    // so we map from the glass quad UV to monitor UV.
    int monitorWidth  = static_cast<int>(monitor->m_transformedSize.x);
    int monitorHeight = static_cast<int>(monitor->m_transformedSize.y);

    float maskThreshold = 0.001f;
    auto threshIt = g_pGlobalState->layerNamespaceMaskThresholds.find(layerSurface->m_namespace);
    if (threshIt != g_pGlobalState->layerNamespaceMaskThresholds.end())
        maskThreshold = threshIt->second;

    GlassRenderer::SMaskInfo maskInfo{
        .textureId      = m_surfaceTempFramebuffer->getTexture()->m_texID,
        .target         = GL_TEXTURE_2D,
        .uvOffset       = {transformBox.x / monitorWidth, transformBox.y / monitorHeight},
        .uvScale        = {transformBox.w / monitorWidth, transformBox.h / monitorHeight},
        .alphaThreshold = maskThreshold,
    };

    // The glass shader composites both the glass effect and the surface content
    // in a single pass: glass behind, surface on top, using the temp FBO alpha.
    GlassRenderer::applyGlassEffect(m_sampleFramebuffer, target,
                                     rawBox, transformBox, alpha,
                                     cornerRadius, roundingPower, m_samplePaddingRatio, ctx,
                                     &maskInfo);
}
