#include "GlassLayerSurface.hpp"
#include "BuiltInPresets.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "LayerGeometry.hpp"

#include <algorithm>
#include <GLES3/gl32.h>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Misc.hpp>

CGlassLayerSurface::CGlassLayerSurface(PHLLS layerSurface)
    : m_layerSurface(layerSurface) {
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

    const bool isAnimating = layerSurface->m_realPosition->isBeingAnimated() ||
                             layerSurface->m_realSize->isBeingAnimated() ||
                             layerSurface->m_alpha->isBeingAnimated() ||
                             layerSurface->m_fadingOut;

    if (isAnimating) {
        auto box = CBox{layerSurface->m_realPosition->value(), layerSurface->m_realSize->value()};
        const auto monitor = layerSurface->m_monitor.lock();
        const float scale = monitor ? monitor->m_scale : 1.0f;
        box.expand(GlassRenderer::SAMPLE_PADDING_PX / scale);
        g_pHyprRenderer->damageBox(box);
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

    auto* source = g_pHyprOpenGL->m_renderData.currentFB;

    auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!layerBox)
        return;

    CBox transformBox = *layerBox;

    const auto transform = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprOpenGL->m_renderData.pMonitor->m_transform));
    transformBox.transform(transform,
        g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.x,
        g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.y);

    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    // During fade-out, re-sampling captures stale pixels. Reuse cached sample if available.
    if (!layerSurface->m_fadingOut) {
        GlassRenderer::sampleBackground(m_sampleFramebuffer, *source, transformBox, m_samplePaddingRatio);

        float blurRadius     = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength) * 12.0f;
        int blurIterations   = std::clamp(static_cast<int>(resolvePresetInt(ctx, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);
        int viewportWidth    = static_cast<int>(g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.x);
        int viewportHeight   = static_cast<int>(g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.y);
        GlassRenderer::blurBackground(m_sampleFramebuffer, blurRadius, blurIterations, source->getFBID(), viewportWidth, viewportHeight);

        m_hasCachedSample = true;
    } else if (!m_hasCachedSample) {
        return;
    }

    // Redirect surface rendering to a temp FBO cleared to transparent.
    // The original renderLayer (called between pre/post elements) will render
    // the surface into this FBO. compositeAndRestore uses its alpha as a mask.
    int monitorWidth  = static_cast<int>(monitor->m_transformedSize.x);
    int monitorHeight = static_cast<int>(monitor->m_transformedSize.y);

    if (m_surfaceTempFramebuffer.m_size.x != monitorWidth || m_surfaceTempFramebuffer.m_size.y != monitorHeight)
        m_surfaceTempFramebuffer.alloc(monitorWidth, monitorHeight, source->m_drmFormat);

    m_savedCurrentFB = source;

    g_pHyprOpenGL->m_renderData.currentFB = &m_surfaceTempFramebuffer;
    glBindFramebuffer(GL_FRAMEBUFFER, m_surfaceTempFramebuffer.getFBID());

    // Disable scissor to clear the entire temp FBO — the render pass scissor
    // would otherwise clip the clear to the current damage region.
    g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void CGlassLayerSurface::compositeAndRestore(PHLMONITOR monitor, float alpha) {
    // Restore the original currentFB before compositing
    if (m_savedCurrentFB) {
        g_pHyprOpenGL->m_renderData.currentFB = m_savedCurrentFB;
        glBindFramebuffer(GL_FRAMEBUFFER, m_savedCurrentFB->getFBID());
        m_savedCurrentFB = nullptr;
    }

    auto& shaderManager = g_pGlobalState->shaderManager;
    if (!shaderManager.isInitialized() || !m_hasCachedSample)
        return;

    const auto layerSurface = m_layerSurface.lock();
    if (!layerSurface)
        return;

    auto* target = g_pHyprOpenGL->m_renderData.currentFB;

    auto layerBox = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!layerBox)
        return;

    CBox rawBox       = *layerBox;
    CBox transformBox = rawBox;

    const auto transform = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprOpenGL->m_renderData.pMonitor->m_transform));
    transformBox.transform(transform,
        g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.x,
        g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.y);

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

    GlassRenderer::SMaskInfo maskInfo{
        .textureId = m_surfaceTempFramebuffer.getTexture()->m_texID,
        .target    = GL_TEXTURE_2D,
        .uvOffset  = {transformBox.x / monitorWidth, transformBox.y / monitorHeight},
        .uvScale   = {transformBox.w / monitorWidth, transformBox.h / monitorHeight},
    };

    // The glass shader composites both the glass effect and the surface content
    // in a single pass: glass behind, surface on top, using the temp FBO alpha.
    GlassRenderer::applyGlassEffect(m_sampleFramebuffer, *target,
                                     rawBox, transformBox, alpha,
                                     cornerRadius, roundingPower, m_samplePaddingRatio, ctx,
                                     &maskInfo);
}
