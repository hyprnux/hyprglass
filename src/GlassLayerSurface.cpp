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

        auto box = CBox{currentPosition, currentSize};
        const auto monitor = layerSurface->m_monitor.lock();
        const float scale = monitor ? monitor->m_scale : 1.0f;
        box.expand(GlassRenderer::SAMPLE_PADDING_PX / scale).noNegativeSize();
        if (box.w > 0.0 && box.h > 0.0)
            g_pHyprRenderer->damageBox(box);

        if (monitor)
            g_pGlobalState->bumpSceneGeneration(monitor);
    }
}

void CGlassLayerSurface::sampleAndRedirect(PHLMONITOR monitor, float alpha) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    shaderManager.initializeIfNeeded();

    if (!shaderManager.isInitialized())
        return;

    const auto layerSurface = m_layerSurface.lock();
    auto source = g_pHyprRenderer->m_renderData.currentFB;
    if (!source)
        return;

    CBox rawBox;
    CBox transformBox;
    bool hasValidBox = false;

    if (layerSurface) {
        auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
        if (layerBox) {
            rawBox = *layerBox;
            transformBox = transformedLayerBox(rawBox, monitor);
            m_lastRawBox = rawBox;
            m_lastTransformBox = transformBox;
            hasValidBox = true;
        }
    }

    if (!hasValidBox) {
        if (m_lastRawBox.w > 0.0 && m_lastRawBox.h > 0.0) {
            rawBox = m_lastRawBox;
            transformBox = m_lastTransformBox;
            hasValidBox = true;
        }
    }

    if (!hasValidBox)
        return;

    // Decide whether we need to re-sample and re-blur the background.
    const uint64_t currentGeneration = g_pGlobalState->getSceneGeneration(monitor);
    const auto activeWs = monitor->m_activeWorkspace;
    const bool isAnimating = layerSurface && (
                             layerSurface->positionAnimation()->isBeingAnimated() ||
                             layerSurface->sizeAnimation()->isBeingAnimated() ||
                             layerSurface->alpha()[Desktop::View::LS_ALPHA_FADE]->isBeingAnimated() ||
                             (activeWs && activeWs->m_renderOffset->isBeingAnimated()));
    const bool isUnmapped = !layerSurface || !layerSurface->m_mapped;
    const bool backgroundChanged = !m_hasCachedSample ||
                                   currentGeneration != m_lastSceneGeneration ||
                                   isAnimating;

    if (!g_pHyprRenderer->m_bRenderingSnapshot && (isUnmapped || backgroundChanged)) {
        const bool isDark          = resolveThemeIsDark();
        const std::string preset   = resolvePresetName();
        const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

        float blurStrength   = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength);
        int downscale        = blurStrength >= GlassRenderer::BLUR_DOWNSCALE_THRESHOLD ? GlassRenderer::BLUR_DOWNSCALE_MAX : 1;

        GlassRenderer::sampleBackground(m_sampleFramebuffer, source, transformBox, m_samplePaddingRatio, downscale);

        float blurRadius     = blurStrength * 12.0f / downscale;
        int blurIterations   = std::clamp(static_cast<int>(resolvePresetInt(ctx, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);
        GlassRenderer::blurBackground(m_sampleFramebuffer, blurRadius, blurIterations, source);

        m_hasCachedSample      = true;
        m_lastSceneGeneration  = currentGeneration;
    }

    int monitorWidth  = static_cast<int>(monitor->m_transformedSize.x);
    int monitorHeight = static_cast<int>(monitor->m_transformedSize.y);

    DRMFormat tempFormat = (monitor->useFP16()) ? source->m_drmFormat : DRM_FORMAT_ARGB8888;

    if (!m_surfaceTempFramebuffer)
        m_surfaceTempFramebuffer = g_pHyprRenderer->createFB("hyprglass-layer-temp");

    if (m_surfaceTempFramebuffer->m_size.x != monitorWidth || m_surfaceTempFramebuffer->m_size.y != monitorHeight ||
        m_surfaceTempFramebuffer->m_drmFormat != tempFormat)
        m_surfaceTempFramebuffer->alloc(monitorWidth, monitorHeight, tempFormat);

    m_savedCurrentFB = source;

    g_pHyprRenderer->m_renderData.currentFB = m_surfaceTempFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, dynamic_cast<Render::GL::CGLFramebuffer*>(m_surfaceTempFramebuffer.get())->getFBID());

    g_pHyprOpenGL->scissor(nullptr);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void CGlassLayerSurface::compositeAndRestore(PHLMONITOR monitor, float alpha) {
    // Restore the original currentFB before compositing
    if (m_savedCurrentFB) {
        g_pHyprRenderer->m_renderData.currentFB = m_savedCurrentFB;
        glBindFramebuffer(GL_FRAMEBUFFER, dynamic_cast<Render::GL::CGLFramebuffer*>(m_savedCurrentFB.get())->getFBID());
        m_savedCurrentFB.reset();
    }

    auto& shaderManager = g_pGlobalState->shaderManager;
    if (!shaderManager.isInitialized() || !m_hasCachedSample)
        return;

    auto target = g_pHyprRenderer->m_renderData.currentFB;
    if (!target)
        return;

    const auto layerSurface = m_layerSurface.lock();
    CBox rawBox;
    CBox transformBox;
    bool hasValidBox = false;

    if (layerSurface) {
        auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
        if (layerBox) {
            rawBox = *layerBox;
            transformBox = transformedLayerBox(rawBox, monitor);
            m_lastRawBox = rawBox;
            m_lastTransformBox = transformBox;
            hasValidBox = true;
        }
    }

    if (!hasValidBox) {
        if (m_lastRawBox.w > 0.0 && m_lastRawBox.h > 0.0) {
            rawBox = m_lastRawBox;
            transformBox = m_lastTransformBox;
            hasValidBox = true;
        }
    }

    if (!hasValidBox)
        return;

    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    float cornerRadius  = 0.0f;
    float roundingPower = 2.0f;

    int monitorWidth  = static_cast<int>(monitor->m_transformedSize.x);
    int monitorHeight = static_cast<int>(monitor->m_transformedSize.y);

    float maskThreshold = 0.001f;
    if (layerSurface) {
        auto threshIt = g_pGlobalState->layerNamespaceMaskThresholds.find(layerSurface->m_namespace);
        if (threshIt != g_pGlobalState->layerNamespaceMaskThresholds.end())
            maskThreshold = threshIt->second;
    }

    maskThreshold *= std::clamp(alpha, 0.0f, 1.0f);

    GlassRenderer::SMaskInfo maskInfo{
        .textureId         = m_surfaceTempFramebuffer->getTexture()->m_texID,
        .target            = GL_TEXTURE_2D,
        .uvOffset          = {transformBox.x / monitorWidth, transformBox.y / monitorHeight},
        .uvScale           = {transformBox.w / monitorWidth, transformBox.h / monitorHeight},
        .alphaThreshold    = maskThreshold,
    };

    GlassRenderer::applyGlassEffect(m_sampleFramebuffer, target,
                                     rawBox, transformBox, alpha,
                                     cornerRadius, roundingPower, m_samplePaddingRatio, ctx,
                                     &maskInfo);
}
