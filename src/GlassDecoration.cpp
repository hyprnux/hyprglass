#include "GlassDecoration.hpp"
#include "GlassPassElement.hpp"
#include "Globals.hpp"
#include "WindowGeometry.hpp"

#include <algorithm>
#include <array>
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
    const auto& config = g_pGlobalState->config;

    try {
        const auto window = m_window.lock();
        if (window && window->m_ruleApplicator) {
            if (window->m_ruleApplicator->m_tagKeeper.isTagged(std::string(TAG_THEME_LIGHT)))
                return false;
            if (window->m_ruleApplicator->m_tagKeeper.isTagged(std::string(TAG_THEME_DARK)))
                return true;
        }
    } catch (...) {}

    return **config.defaultTheme == 0;
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

        // Workspace slide animations need continuous damage
        if (workspace && !window->m_pinned && workspace->m_renderOffset->isBeingAnimated())
            damageEntire();

        // Damage when position or size actually changed since last frame.
        // This seeds initial damage (first frame detects change from default {0,0}),
        // keeps the glass updated during movement, and naturally stops when
        // static (same position → no damage → no feedback loop).
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

void CGlassDecoration::sampleBackground(CFramebuffer& sourceFramebuffer, CBox box) {
    const int pad = SAMPLE_PADDING_PX;
    int paddedWidth  = static_cast<int>(box.width) + 2 * pad;
    int paddedHeight = static_cast<int>(box.height) + 2 * pad;

    if (m_sampleFramebuffer.m_size.x != paddedWidth || m_sampleFramebuffer.m_size.y != paddedHeight)
        m_sampleFramebuffer.alloc(paddedWidth, paddedHeight, sourceFramebuffer.m_drmFormat);

    int srcX0 = static_cast<int>(box.x) - pad;
    int srcX1 = static_cast<int>(box.x + box.width) + pad;
    int srcY0 = static_cast<int>(box.y) - pad;
    int srcY1 = static_cast<int>(box.y + box.height) + pad;

    // Clamp source coordinates to framebuffer bounds to avoid reading black/undefined pixels
    int framebufferWidth  = static_cast<int>(sourceFramebuffer.m_size.x);
    int framebufferHeight = static_cast<int>(sourceFramebuffer.m_size.y);

    int dstX0 = 0, dstY0 = 0, dstX1 = paddedWidth, dstY1 = paddedHeight;

    if (srcX0 < 0) { dstX0 += -srcX0; srcX0 = 0; }
    if (srcY0 < 0) { dstY0 += -srcY0; srcY0 = 0; }
    if (srcX1 > framebufferWidth)  { dstX1 -= (srcX1 - framebufferWidth);  srcX1 = framebufferWidth; }
    if (srcY1 > framebufferHeight) { dstY1 -= (srcY1 - framebufferHeight); srcY1 = framebufferHeight; }

    m_samplePaddingRatio = Vector2D(
        static_cast<double>(pad) / paddedWidth,
        static_cast<double>(pad) / paddedHeight
    );

    // The render pass scissors each element to its damage region.
    // That scissor state leaks here and clips glBlitFramebuffer on the
    // DRAW framebuffer, causing partial writes and stale noise artifacts.
    glDisable(GL_SCISSOR_TEST);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFramebuffer.getFBID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sampleFramebuffer.getFBID());
    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                      dstX0, dstY0, dstX1, dstY1,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void CGlassDecoration::blurBackground(float radius, int iterations, GLuint callerFramebufferID, int viewportWidth, int viewportHeight) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    if (radius <= 0.0f || iterations <= 0 || !shaderManager.isInitialized())
        return;

    int width  = static_cast<int>(m_sampleFramebuffer.m_size.x);
    int height = static_cast<int>(m_sampleFramebuffer.m_size.y);

    auto& blurTempFramebuffer = g_pGlobalState->blurTempFramebuffer;
    if (blurTempFramebuffer.m_size.x != width || blurTempFramebuffer.m_size.y != height)
        blurTempFramebuffer.alloc(width, height, m_sampleFramebuffer.m_drmFormat);

    // Fullscreen quad projection: maps VAO positions [0,1] to clip space [-1,1]
    static constexpr std::array<float, 9> FULLSCREEN_PROJECTION = {
        2.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f,
       -1.0f,-1.0f, 1.0f,
    };

    auto& blurShader = shaderManager.blurShader;
    const auto& blurUniforms = shaderManager.blurUniforms;

    g_pHyprOpenGL->useProgram(blurShader.program);
    blurShader.setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, FULLSCREEN_PROJECTION);
    blurShader.setUniformInt(SHADER_TEX, 0);
    glUniform1f(blurUniforms.radius, radius);
    glBindVertexArray(blurShader.uniformLocations[SHADER_SHADER_VAO]);
    glViewport(0, 0, width, height);
    glActiveTexture(GL_TEXTURE0);

    // Ping-pong at full resolution: m_sampleFramebuffer ↔ blurTempFramebuffer
    for (int iteration = 0; iteration < iterations; iteration++) {
        // Horizontal pass: m_sampleFramebuffer → blurTempFramebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, blurTempFramebuffer.getFBID());
        m_sampleFramebuffer.getTexture()->bind();
        glUniform2f(blurUniforms.direction, 1.0f / width, 0.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Vertical pass: blurTempFramebuffer → m_sampleFramebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, m_sampleFramebuffer.getFBID());
        blurTempFramebuffer.getTexture()->bind();
        glUniform2f(blurUniforms.direction, 0.0f, 1.0f / height);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // Restore caller's GL state without querying (avoids pipeline stalls)
    glBindFramebuffer(GL_FRAMEBUFFER, callerFramebufferID);
    glBindVertexArray(0);
    glViewport(0, 0, viewportWidth, viewportHeight);
}

void CGlassDecoration::uploadThemeUniforms(bool isDark) const {
    const auto& config   = g_pGlobalState->config;
    const auto& uniforms = g_pGlobalState->shaderManager.glassUniforms;
    const auto& theme    = isDark ? config.dark : config.light;
    const auto& defaults = isDark ? DARK_THEME_DEFAULTS : LIGHT_THEME_DEFAULTS;

    glUniform1f(uniforms.brightness,       resolveFloat(theme.brightness, config.global.brightness, defaults.brightness));
    glUniform1f(uniforms.contrast,         resolveFloat(theme.contrast, config.global.contrast, defaults.contrast));
    glUniform1f(uniforms.saturation,       resolveFloat(theme.saturation, config.global.saturation, defaults.saturation));
    glUniform1f(uniforms.vibrancy,         resolveFloat(theme.vibrancy, config.global.vibrancy, defaults.vibrancy));
    glUniform1f(uniforms.vibrancyDarkness, resolveFloat(theme.vibrancyDarkness, config.global.vibrancyDarkness, defaults.vibrancyDarkness));

    // adaptive_correction maps to the appropriate shader uniform per theme
    const float adaptiveCorrection = resolveFloat(theme.adaptiveCorrection, config.global.adaptiveCorrection, defaults.adaptiveCorrection);
    if (isDark) {
        glUniform1f(uniforms.adaptiveDim,   adaptiveCorrection);
        glUniform1f(uniforms.adaptiveBoost, 0.0f);
    } else {
        glUniform1f(uniforms.adaptiveDim,   0.0f);
        glUniform1f(uniforms.adaptiveBoost, adaptiveCorrection);
    }
}

void CGlassDecoration::applyGlassEffect(CFramebuffer& sourceFramebuffer, CFramebuffer& targetFramebuffer,
                                         CBox& rawBox, CBox& transformedBox, float windowAlpha) {
    const auto& config   = g_pGlobalState->config;
    auto& shaderManager  = g_pGlobalState->shaderManager;
    const auto& uniforms = shaderManager.glassUniforms;

    const bool isDark = resolveThemeIsDark();
    const auto& theme = isDark ? config.dark : config.light;

    const auto transform = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprOpenGL->m_renderData.pMonitor->m_transform));

    Mat3x3 matrix   = g_pHyprOpenGL->m_renderData.monitorProjection.projectBox(rawBox, transform, rawBox.rot);
    Mat3x3 glMatrix = g_pHyprOpenGL->m_renderData.projection.copy().multiply(matrix);
    auto texture    = sourceFramebuffer.getTexture();

    glMatrix.transpose();

    glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer.getFBID());
    glActiveTexture(GL_TEXTURE0);
    texture->bind();

    g_pHyprOpenGL->useProgram(shaderManager.glassShader.program);

    shaderManager.glassShader.setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, glMatrix.getMatrix());
    shaderManager.glassShader.setUniformInt(SHADER_TEX, 0);

    const auto fullSize = Vector2D(transformedBox.width, transformedBox.height);
    shaderManager.glassShader.setUniformFloat2(SHADER_FULL_SIZE,
        static_cast<float>(fullSize.x), static_cast<float>(fullSize.y));

    glUniform1f(uniforms.refractionStrength,  resolveFloat(theme.refractionStrength, config.global.refractionStrength));
    glUniform1f(uniforms.chromaticAberration, resolveFloat(theme.chromaticAberration, config.global.chromaticAberration));
    glUniform1f(uniforms.fresnelStrength,     resolveFloat(theme.fresnelStrength, config.global.fresnelStrength));
    glUniform1f(uniforms.specularStrength,    resolveFloat(theme.specularStrength, config.global.specularStrength));
    glUniform1f(uniforms.glassOpacity,        resolveFloat(theme.glassOpacity, config.global.glassOpacity) * windowAlpha);
    glUniform1f(uniforms.edgeThickness,       resolveFloat(theme.edgeThickness, config.global.edgeThickness));
    glUniform1f(uniforms.lensDistortion,      resolveFloat(theme.lensDistortion, config.global.lensDistortion));

    uploadThemeUniforms(isDark);

    const int64_t tintColorValue = resolveInt(theme.tintColor, config.global.tintColor);
    glUniform3f(uniforms.tintColor,
        static_cast<float>((tintColorValue >> 24) & 0xFF) / 255.0f,
        static_cast<float>((tintColorValue >> 16) & 0xFF) / 255.0f,
        static_cast<float>((tintColorValue >> 8) & 0xFF) / 255.0f);
    glUniform1f(uniforms.tintAlpha,
        static_cast<float>(tintColorValue & 0xFF) / 255.0f);

    glUniform2f(uniforms.uvPadding,
        static_cast<float>(m_samplePaddingRatio.x),
        static_cast<float>(m_samplePaddingRatio.y));

    const auto window = m_window.lock();
    float monitorScale = g_pHyprOpenGL->m_renderData.pMonitor->m_scale;
    float cornerRadius  = window ? window->rounding() * monitorScale : 0.0f;
    float roundingPower = window ? window->roundingPower() : 2.0f;
    shaderManager.glassShader.setUniformFloat(SHADER_RADIUS, cornerRadius);
    glUniform1f(uniforms.roundingPower, roundingPower);

    glBindVertexArray(shaderManager.glassShader.uniformLocations[SHADER_SHADER_VAO]);
    g_pHyprOpenGL->scissor(rawBox);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    g_pHyprOpenGL->scissor(nullptr);
}

void CGlassDecoration::renderPass(PHLMONITOR monitor, const float& alpha) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    shaderManager.initializeIfNeeded();

    if (!shaderManager.isInitialized())
        return;

    const auto window = m_window.lock();
    if (!window)
        return;

    const auto workspace = window->m_workspace;
    const auto workspaceOffset = workspace && !window->m_pinned
        ? workspace->m_renderOffset->value()
        : Vector2D();

    const auto source = g_pHyprOpenGL->m_renderData.currentFB;

    CBox windowBox = window->getWindowMainSurfaceBox()
                         .translate(workspaceOffset)
                         .translate(-monitor->m_position + window->m_floatingOffset)
                         .scale(monitor->m_scale)
                         .round();
    CBox transformBox = windowBox;

    const auto transform = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprOpenGL->m_renderData.pMonitor->m_transform));
    transformBox.transform(transform,
        g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.x,
        g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.y);

    sampleBackground(*source, transformBox);

    const auto& config = g_pGlobalState->config;
    const bool isDark = resolveThemeIsDark();
    const auto& theme = isDark ? config.dark : config.light;

    float blurRadius     = resolveFloat(theme.blurStrength, config.global.blurStrength) * 12.0f;
    int blurIterations   = std::clamp(static_cast<int>(resolveInt(theme.blurIterations, config.global.blurIterations)), 1, 5);
    int viewportWidth    = static_cast<int>(g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.x);
    int viewportHeight   = static_cast<int>(g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.y);
    blurBackground(blurRadius, blurIterations, source->getFBID(), viewportWidth, viewportHeight);

    applyGlassEffect(m_sampleFramebuffer, *source, windowBox, transformBox, alpha);
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
    surfaceBox.expand(SAMPLE_PADDING_PX / scale);

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
