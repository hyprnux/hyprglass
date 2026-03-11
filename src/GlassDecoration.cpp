#include "GlassDecoration.hpp"
#include "BuiltInPresets.hpp"
#include "GlassPassElement.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "WindowGeometry.hpp"

#include <algorithm>
#include <GLES3/gl32.h>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/desktop/rule/windowRule/WindowRuleApplicator.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Misc.hpp>

CGlassDecoration::CGlassDecoration(PHLWINDOW window)
    : IHyprWindowDecoration(window), m_window(window) {
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
        if (config.defaultTheme) {
            const char* theme = *config.defaultTheme;
            if (theme)
                return std::string_view(theme) != "light";
        }
    } catch (...) {}

    return true;
}

std::string CGlassDecoration::resolvePresetName() const {
    try {
        const auto window = m_window.lock();
        if (window && window->m_ruleApplicator) {
            for (const auto& tag : window->m_ruleApplicator->m_tagKeeper.getTags()) {
                if (tag.starts_with(TAG_PRESET_PREFIX))
                    return tag.substr(TAG_PRESET_PREFIX.size());
            }
        }

        const auto& config = g_pGlobalState->config;
        if (config.defaultPreset) {
            const char* preset = *config.defaultPreset;
            if (preset && preset[0] != '\0')
                return std::string(preset);
        }
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

void CGlassDecoration::draw(PHLMONITOR monitor, float const& alpha) {
    if (!**g_pGlobalState->config.enabled)
        return;

    CGlassPassElement::SGlassPassData data{this, alpha};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CGlassPassElement>(data));

    const auto window = m_window.lock();
    if (window) {
        const auto workspace = window->m_workspace;

        if (workspace && !window->m_pinned && workspace->m_renderOffset->isBeingAnimated())
            damageEntire();

        const auto currentPosition = window->m_realPosition->value();
        const auto currentSize = window->m_realSize->value();
        if (currentPosition != m_lastPosition || currentSize != m_lastSize) {
            damageEntire();
            m_lastPosition = currentPosition;
            m_lastSize = currentSize;
        }
    }
}

PHLWINDOW CGlassDecoration::getOwner() {
    return m_window.lock();
}

void CGlassDecoration::renderPass(PHLMONITOR monitor, const float& alpha) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    shaderManager.initializeIfNeeded();

    if (!shaderManager.isInitialized())
        return;

    const auto window = m_window.lock();
    if (!window)
        return;

    const auto source = g_pHyprOpenGL->m_renderData.currentFB;

    auto optBox = WindowGeometry::computeWindowBox(window, monitor);
    if (!optBox)
        return;

    CBox windowBox    = *optBox;
    CBox transformBox = windowBox;

    const auto transform = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprOpenGL->m_renderData.pMonitor->m_transform));
    transformBox.transform(transform,
        g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.x,
        g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.y);

    GlassRenderer::sampleBackground(m_sampleFramebuffer, *source, transformBox, m_samplePaddingRatio);

    const bool isDark          = resolveThemeIsDark();
    const std::string preset   = resolvePresetName();
    const SResolveContext ctx  = {preset, isDark, g_pGlobalState->config, g_pGlobalState->customPresets};

    float blurRadius     = resolvePresetFloat(ctx, &SPresetValues::blurStrength, &SOverridableConfig::blurStrength) * 12.0f;
    int blurIterations   = std::clamp(static_cast<int>(resolvePresetInt(ctx, &SPresetValues::blurIterations, &SOverridableConfig::blurIterations)), 1, 5);
    int viewportWidth    = static_cast<int>(g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.x);
    int viewportHeight   = static_cast<int>(g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.y);
    GlassRenderer::blurBackground(m_sampleFramebuffer, blurRadius, blurIterations, source->getFBID(), viewportWidth, viewportHeight);

    float monitorScale  = monitor->m_scale;
    float cornerRadius  = window->rounding() * monitorScale;
    float roundingPower = window->roundingPower();

    GlassRenderer::applyGlassEffect(m_sampleFramebuffer, *source,
                                     windowBox, transformBox, alpha,
                                     cornerRadius, roundingPower, m_samplePaddingRatio, ctx);
}

eDecorationType CGlassDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CGlassDecoration::updateWindow(PHLWINDOW window) {
    damageEntire();
}

void CGlassDecoration::damageEntire() {
    const auto window = m_window.lock();
    if (!window)
        return;

    const auto workspace = window->m_workspace;
    auto surfaceBox = window->getWindowMainSurfaceBox();

    if (workspace && workspace->m_renderOffset->isBeingAnimated() && !window->m_pinned)
        surfaceBox.translate(workspace->m_renderOffset->value());
    surfaceBox.translate(window->m_floatingOffset);

    // Expand damage by our sampling padding so the render pass re-renders
    // background content (wallpaper, other windows) in the padded margin.
    // Without this, the scissored render pass leaves stale previous-frame
    // content in the padding area, causing noise artifacts.
    // surfaceBox is in logical coords; convert pixel padding to logical.
    const auto monitor = window->m_monitor.lock();
    const float scale = monitor ? monitor->m_scale : 1.0f;
    surfaceBox.expand(GlassRenderer::SAMPLE_PADDING_PX / scale);

    g_pHyprRenderer->damageBox(surfaceBox);
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
