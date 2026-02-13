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

    // Only damage during active workspace animations so the glass effect
    // updates smoothly during transitions but goes idle once complete.
    const auto window = m_window.lock();
    if (window) {
        const auto workspace = window->m_workspace;
        if (workspace && !window->m_pinned && workspace->m_renderOffset->isBeingAnimated())
            damageEntire();
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

    int fullWidth  = static_cast<int>(m_sampleFramebuffer.m_size.x);
    int fullHeight = static_cast<int>(m_sampleFramebuffer.m_size.y);

    // Blur at half resolution to cut fragment work by ~75%
    int halfWidth    = std::max(1, fullWidth / 2);
    int halfHeight   = std::max(1, fullHeight / 2);
    float scaledRadius = radius * 0.5f;

    auto& blurFramebuffer     = g_pGlobalState->blurHalfFramebuffer;
    auto& blurTempFramebuffer = g_pGlobalState->blurHalfTempFramebuffer;

    if (blurFramebuffer.m_size.x != halfWidth || blurFramebuffer.m_size.y != halfHeight)
        blurFramebuffer.alloc(halfWidth, halfHeight, m_sampleFramebuffer.m_drmFormat);
    if (blurTempFramebuffer.m_size.x != halfWidth || blurTempFramebuffer.m_size.y != halfHeight)
        blurTempFramebuffer.alloc(halfWidth, halfHeight, m_sampleFramebuffer.m_drmFormat);

    // Downsample: blit full-res sample → half-res (hardware-accelerated bilinear downscale)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sampleFramebuffer.getFBID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, blurFramebuffer.getFBID());
    glBlitFramebuffer(0, 0, fullWidth, fullHeight, 0, 0, halfWidth, halfHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

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
    glUniform1f(blurUniforms.radius, scaledRadius);
    glBindVertexArray(blurShader.uniformLocations[SHADER_SHADER_VAO]);
    glViewport(0, 0, halfWidth, halfHeight);
    glActiveTexture(GL_TEXTURE0);

    for (int iteration = 0; iteration < iterations; iteration++) {
        // Horizontal pass: blurFramebuffer → blurTempFramebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, blurTempFramebuffer.getFBID());
        blurFramebuffer.getTexture()->bind();
        glUniform2f(blurUniforms.direction, 1.0f / halfWidth, 0.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Vertical pass: blurTempFramebuffer → blurFramebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, blurFramebuffer.getFBID());
        blurTempFramebuffer.getTexture()->bind();
        glUniform2f(blurUniforms.direction, 0.0f, 1.0f / halfHeight);
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

    if (isDark) {
        glUniform1f(uniforms.brightness,       static_cast<float>(**config.darkBrightness));
        glUniform1f(uniforms.contrast,         static_cast<float>(**config.darkContrast));
        glUniform1f(uniforms.saturation,       static_cast<float>(**config.darkSaturation));
        glUniform1f(uniforms.vibrancy,         static_cast<float>(**config.darkVibrancy));
        glUniform1f(uniforms.vibrancyDarkness, static_cast<float>(**config.darkVibrancyDarkness));
        glUniform1f(uniforms.adaptiveDim,      static_cast<float>(**config.darkAdaptiveDim));
        glUniform1f(uniforms.adaptiveBoost,    0.0f);
    } else {
        glUniform1f(uniforms.brightness,       static_cast<float>(**config.lightBrightness));
        glUniform1f(uniforms.contrast,         static_cast<float>(**config.lightContrast));
        glUniform1f(uniforms.saturation,       static_cast<float>(**config.lightSaturation));
        glUniform1f(uniforms.vibrancy,         static_cast<float>(**config.lightVibrancy));
        glUniform1f(uniforms.vibrancyDarkness, static_cast<float>(**config.lightVibrancyDarkness));
        glUniform1f(uniforms.adaptiveDim,      0.0f);
        glUniform1f(uniforms.adaptiveBoost,    static_cast<float>(**config.lightAdaptiveBoost));
    }
}

void CGlassDecoration::applyGlassEffect(CFramebuffer& sourceFramebuffer, CFramebuffer& targetFramebuffer,
                                         CBox& rawBox, CBox& transformedBox, float windowAlpha) {
    const auto& config   = g_pGlobalState->config;
    auto& shaderManager  = g_pGlobalState->shaderManager;
    const auto& uniforms = shaderManager.glassUniforms;

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

    glUniform1f(uniforms.refractionStrength,  static_cast<float>(**config.refractionStrength));
    glUniform1f(uniforms.chromaticAberration, static_cast<float>(**config.chromaticAberration));
    glUniform1f(uniforms.fresnelStrength,     static_cast<float>(**config.fresnelStrength));
    glUniform1f(uniforms.specularStrength,    static_cast<float>(**config.specularStrength));
    glUniform1f(uniforms.glassOpacity,        static_cast<float>(**config.glassOpacity) * windowAlpha);
    glUniform1f(uniforms.edgeThickness,       static_cast<float>(**config.edgeThickness));
    glUniform1f(uniforms.lensDistortion,      static_cast<float>(**config.lensDistortion));

    uploadThemeUniforms(resolveThemeIsDark());

    const int64_t tintColorValue = **config.tintColor;
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
    float blurRadius     = static_cast<float>(**config.blurStrength) * 12.0f;
    int blurIterations   = std::clamp(static_cast<int>(**config.blurIterations), 1, 5);
    int viewportWidth    = static_cast<int>(g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.x);
    int viewportHeight   = static_cast<int>(g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.y);
    blurBackground(blurRadius, blurIterations, source->getFBID(), viewportWidth, viewportHeight);

    applyGlassEffect(g_pGlobalState->blurHalfFramebuffer, *source, windowBox, transformBox, alpha);
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
