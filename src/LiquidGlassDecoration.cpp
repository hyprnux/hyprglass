#include "LiquidGlassDecoration.hpp"
#include "LiquidGlassPassElement.hpp"
#include "globals.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Misc.hpp>
#include <chrono>

CLiquidGlassDecoration::CLiquidGlassDecoration(PHLWINDOW pWindow)
    : IHyprWindowDecoration(pWindow), m_pWindow(pWindow) {
}

SDecorationPositioningInfo CLiquidGlassDecoration::getPositioningInfo() {
    SDecorationPositioningInfo info;
    info.priority       = 10000;
    info.policy         = DECORATION_POSITION_ABSOLUTE;
    info.desiredExtents = {{0, 0}, {0, 0}};
    return info;
}

void CLiquidGlassDecoration::onPositioningReply(const SDecorationPositioningReply& reply) {}

void CLiquidGlassDecoration::draw(PHLMONITOR pMonitor, float const& a) {
    static auto* const PENABLED = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:liquid-glass:enabled")->getDataStaticPtr();
    if (!**PENABLED)
        return;

    CLiquidGlassPassElement::SLiquidGlassData data{this, a};
    g_pHyprRenderer->m_renderPass.add(makeUnique<CLiquidGlassPassElement>(data));
}

PHLWINDOW CLiquidGlassDecoration::getOwner() {
    return m_pWindow.lock();
}

void CLiquidGlassDecoration::sampleBackground(CFramebuffer& sourceFB, CBox box) {
    const int pad = SAMPLE_PADDING_PX;
    int paddedW = static_cast<int>(box.width) + 2 * pad;
    int paddedH = static_cast<int>(box.height) + 2 * pad;

    if (m_sampleFB.m_size.x != paddedW || m_sampleFB.m_size.y != paddedH) {
        m_sampleFB.alloc(paddedW, paddedH, sourceFB.m_drmFormat);
    }

    int x0 = static_cast<int>(box.x) - pad;
    int x1 = static_cast<int>(box.x + box.width) + pad;
    int y0 = static_cast<int>(box.y) - pad;
    int y1 = static_cast<int>(box.y + box.height) + pad;

    m_samplePaddingRatio = Vector2D(
        static_cast<double>(pad) / paddedW,
        static_cast<double>(pad) / paddedH
    );

    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFB.getFBID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sampleFB.getFBID());
    glBlitFramebuffer(x0, y0, x1, y1, 0, 0,
                      paddedW, paddedH,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void CLiquidGlassDecoration::blurBackground(float radius, int iterations) {
    if (radius <= 0.0f || iterations <= 0 || !g_pGlobalState->blurShaderInitialized)
        return;

    int w = static_cast<int>(m_sampleFB.m_size.x);
    int h = static_cast<int>(m_sampleFB.m_size.y);

    if (m_blurTmpFB.m_size.x != w || m_blurTmpFB.m_size.y != h)
        m_blurTmpFB.alloc(w, h, m_sampleFB.m_drmFormat);

    // Save GL state that we modify
    GLint prevFB = 0, prevVAO = 0;
    GLint prevViewport[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFB);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    glGetIntegerv(GL_VIEWPORT, prevViewport);

    // Fullscreen quad projection: maps VAO positions [0,1] to clip space [-1,1]
    std::array<float, 9> projMatrix = {
        2.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f,
       -1.0f,-1.0f, 1.0f,
    };

    auto& blurShader = g_pGlobalState->blurShader;

    g_pHyprOpenGL->useProgram(blurShader.program);
    blurShader.setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, projMatrix);
    blurShader.setUniformInt(SHADER_TEX, 0);
    glUniform1f(g_pGlobalState->locBlurRadius, radius);
    glBindVertexArray(blurShader.uniformLocations[SHADER_SHADER_VAO]);
    glViewport(0, 0, w, h);
    glActiveTexture(GL_TEXTURE0);

    for (int iter = 0; iter < iterations; iter++) {
        // H pass: m_sampleFB → m_blurTmpFB
        glBindFramebuffer(GL_FRAMEBUFFER, m_blurTmpFB.getFBID());
        m_sampleFB.getTexture()->bind();
        glUniform2f(g_pGlobalState->locBlurDirection, 1.0f / w, 0.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // V pass: m_blurTmpFB → m_sampleFB
        glBindFramebuffer(GL_FRAMEBUFFER, m_sampleFB.getFBID());
        m_blurTmpFB.getTexture()->bind();
        glUniform2f(g_pGlobalState->locBlurDirection, 0.0f, 1.0f / h);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFB);
    glBindVertexArray(prevVAO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
}

void CLiquidGlassDecoration::applyLiquidGlassEffect(CFramebuffer& sourceFB, CFramebuffer& targetFB,
                                                      CBox& rawBox, CBox& transformedBox, float windowAlpha) {
    static auto* const PBLUR       = (Hyprlang::FLOAT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:liquid-glass:blur_strength")->getDataStaticPtr();
    static auto* const PREFRACT    = (Hyprlang::FLOAT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:liquid-glass:refraction_strength")->getDataStaticPtr();
    static auto* const PCHROMATIC  = (Hyprlang::FLOAT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:liquid-glass:chromatic_aberration")->getDataStaticPtr();
    static auto* const PFRESNEL    = (Hyprlang::FLOAT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:liquid-glass:fresnel_strength")->getDataStaticPtr();
    static auto* const PSPECULAR   = (Hyprlang::FLOAT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:liquid-glass:specular_strength")->getDataStaticPtr();
    static auto* const POPACITY    = (Hyprlang::FLOAT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:liquid-glass:glass_opacity")->getDataStaticPtr();
    static auto* const PEDGE       = (Hyprlang::FLOAT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:liquid-glass:edge_thickness")->getDataStaticPtr();

    const auto TR = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprOpenGL->m_renderData.pMonitor->m_transform));

    Mat3x3 matrix = g_pHyprOpenGL->m_renderData.monitorProjection.projectBox(rawBox, TR, rawBox.rot);
    Mat3x3 glMatrix = g_pHyprOpenGL->m_renderData.projection.copy().multiply(matrix);
    auto tex = sourceFB.getTexture();

    glMatrix.transpose();

    glBindFramebuffer(GL_FRAMEBUFFER, targetFB.getFBID());
    glActiveTexture(GL_TEXTURE0);
    tex->bind();

    g_pHyprOpenGL->useProgram(g_pGlobalState->shader.program);

    g_pGlobalState->shader.setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, glMatrix.getMatrix());
    g_pGlobalState->shader.setUniformInt(SHADER_TEX, 0);

    const auto TOPLEFT  = Vector2D(transformedBox.x, transformedBox.y);
    const auto FULLSIZE = Vector2D(transformedBox.width, transformedBox.height);

    g_pGlobalState->shader.setUniformFloat2(SHADER_TOP_LEFT,
        static_cast<float>(TOPLEFT.x), static_cast<float>(TOPLEFT.y));
    g_pGlobalState->shader.setUniformFloat2(SHADER_FULL_SIZE,
        static_cast<float>(FULLSIZE.x), static_cast<float>(FULLSIZE.y));

    auto now = std::chrono::steady_clock::now();
    float time = std::chrono::duration<float>(now.time_since_epoch()).count() - g_pGlobalState->startTime;

    glUniform1f(g_pGlobalState->locTime, time);
    glUniform1f(g_pGlobalState->locBlurStrength, static_cast<float>(**PBLUR));
    glUniform1f(g_pGlobalState->locRefractionStrength, static_cast<float>(**PREFRACT));
    glUniform1f(g_pGlobalState->locChromaticAberration, static_cast<float>(**PCHROMATIC));
    glUniform1f(g_pGlobalState->locFresnelStrength, static_cast<float>(**PFRESNEL));
    glUniform1f(g_pGlobalState->locSpecularStrength, static_cast<float>(**PSPECULAR));
    glUniform1f(g_pGlobalState->locGlassOpacity, static_cast<float>(**POPACITY) * windowAlpha);
    glUniform1f(g_pGlobalState->locEdgeThickness, static_cast<float>(**PEDGE));

    glUniform2f(g_pGlobalState->locFullSizeUntransformed,
        static_cast<float>(rawBox.width), static_cast<float>(rawBox.height));

    glUniform2f(g_pGlobalState->locUvPadding,
        static_cast<float>(m_samplePaddingRatio.x),
        static_cast<float>(m_samplePaddingRatio.y));

    const auto PWINDOW = m_pWindow.lock();
    float cornerRadius = PWINDOW ? PWINDOW->rounding() : 0.0f;
    g_pGlobalState->shader.setUniformFloat(SHADER_RADIUS, cornerRadius);

    glBindVertexArray(g_pGlobalState->shader.uniformLocations[SHADER_SHADER_VAO]);
    g_pHyprOpenGL->scissor(rawBox);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    g_pHyprOpenGL->scissor(nullptr);
}

void CLiquidGlassDecoration::renderPass(PHLMONITOR pMonitor, const float& a) {
    initShaderIfNeeded();

    if (!g_pGlobalState || !g_pGlobalState->shaderInitialized)
        return;

    const auto PWINDOW = m_pWindow.lock();
    if (!PWINDOW)
        return;

    const auto PWORKSPACE = PWINDOW->m_workspace;
    const auto WORKSPACEOFFSET = PWORKSPACE && !PWINDOW->m_pinned
        ? PWORKSPACE->m_renderOffset->value()
        : Vector2D();

    const auto SOURCE = g_pHyprOpenGL->m_renderData.currentFB;

    auto thisbox = PWINDOW->getWindowMainSurfaceBox();

    CBox wlrbox = thisbox.translate(WORKSPACEOFFSET)
                      .translate(-pMonitor->m_position + PWINDOW->m_floatingOffset)
                      .scale(pMonitor->m_scale)
                      .round();
    CBox transformBox = wlrbox;

    const auto TR = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprOpenGL->m_renderData.pMonitor->m_transform));
    transformBox.transform(TR,
        g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.x,
        g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.y);

    sampleBackground(*SOURCE, transformBox);

    static auto* const PBLUR = (Hyprlang::FLOAT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:liquid-glass:blur_strength")->getDataStaticPtr();
    float blurRadius = static_cast<float>(**PBLUR) * 12.0f;
    blurBackground(blurRadius, 3);

    applyLiquidGlassEffect(m_sampleFB, *SOURCE, wlrbox, transformBox, a);
}

eDecorationType CLiquidGlassDecoration::getDecorationType() {
    return DECORATION_CUSTOM;
}

void CLiquidGlassDecoration::updateWindow(PHLWINDOW pWindow) {
    damageEntire();
}

void CLiquidGlassDecoration::damageEntire() {
    const auto PWINDOW = m_pWindow.lock();
    if (!PWINDOW)
        return;

    const auto PWINDOWWORKSPACE = PWINDOW->m_workspace;
    auto surfaceBox = PWINDOW->getWindowMainSurfaceBox();

    if (PWINDOWWORKSPACE && PWINDOWWORKSPACE->m_renderOffset->isBeingAnimated() && !PWINDOW->m_pinned)
        surfaceBox.translate(PWINDOWWORKSPACE->m_renderOffset->value());
    surfaceBox.translate(PWINDOW->m_floatingOffset);

    g_pHyprRenderer->damageBox(surfaceBox);
}

eDecorationLayer CLiquidGlassDecoration::getDecorationLayer() {
    return DECORATION_LAYER_BOTTOM;
}

uint64_t CLiquidGlassDecoration::getDecorationFlags() {
    return DECORATION_NON_SOLID;
}

std::string CLiquidGlassDecoration::getDisplayName() {
    return "LiquidGlass";
}
