#include "LiquidGlassDecoration.hpp"
#include "LiquidGlassPassElement.hpp"
#include "globals.hpp"

#include <algorithm>
#include <GLES3/gl32.h>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/math/Misc.hpp>

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

    // Ensure our window area is damaged for the next frame so the render pass
    // always has our region in m_damage. This is required because needsLiveBlur
    // only expands damage that already intersects our bounding box.
    damageEntire();
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

    int srcX0 = static_cast<int>(box.x) - pad;
    int srcX1 = static_cast<int>(box.x + box.width) + pad;
    int srcY0 = static_cast<int>(box.y) - pad;
    int srcY1 = static_cast<int>(box.y + box.height) + pad;

    // Clamp source coordinates to framebuffer bounds to avoid reading black/undefined pixels
    int fbW = static_cast<int>(sourceFB.m_size.x);
    int fbH = static_cast<int>(sourceFB.m_size.y);

    int dstX0 = 0, dstY0 = 0, dstX1 = paddedW, dstY1 = paddedH;

    if (srcX0 < 0) { dstX0 += -srcX0; srcX0 = 0; }
    if (srcY0 < 0) { dstY0 += -srcY0; srcY0 = 0; }
    if (srcX1 > fbW) { dstX1 -= (srcX1 - fbW); srcX1 = fbW; }
    if (srcY1 > fbH) { dstY1 -= (srcY1 - fbH); srcY1 = fbH; }

    m_samplePaddingRatio = Vector2D(
        static_cast<double>(pad) / paddedW,
        static_cast<double>(pad) / paddedH
    );

    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFB.getFBID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_sampleFB.getFBID());
    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                      dstX0, dstY0, dstX1, dstY1,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void CLiquidGlassDecoration::blurBackground(float radius, int iterations, GLuint callerFB, int viewportW, int viewportH) {
    if (radius <= 0.0f || iterations <= 0 || !g_pGlobalState->blurShaderInitialized)
        return;

    int fullW = static_cast<int>(m_sampleFB.m_size.x);
    int fullH = static_cast<int>(m_sampleFB.m_size.y);

    // Blur at half resolution to cut fragment work by ~75%
    int halfW = std::max(1, fullW / 2);
    int halfH = std::max(1, fullH / 2);
    float halfRadius = radius * 0.5f;

    if (m_blurHalfFB.m_size.x != halfW || m_blurHalfFB.m_size.y != halfH)
        m_blurHalfFB.alloc(halfW, halfH, m_sampleFB.m_drmFormat);
    if (m_blurHalfTmpFB.m_size.x != halfW || m_blurHalfTmpFB.m_size.y != halfH)
        m_blurHalfTmpFB.alloc(halfW, halfH, m_sampleFB.m_drmFormat);

    // Downsample: blit full-res sample → half-res (hardware-accelerated bilinear downscale)
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_sampleFB.getFBID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blurHalfFB.getFBID());
    glBlitFramebuffer(0, 0, fullW, fullH, 0, 0, halfW, halfH, GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Fullscreen quad projection: maps VAO positions [0,1] to clip space [-1,1]
    static constexpr std::array<float, 9> projMatrix = {
        2.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f,
       -1.0f,-1.0f, 1.0f,
    };

    auto& blurShader = g_pGlobalState->blurShader;

    g_pHyprOpenGL->useProgram(blurShader.program);
    blurShader.setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, projMatrix);
    blurShader.setUniformInt(SHADER_TEX, 0);
    glUniform1f(g_pGlobalState->locBlurRadius, halfRadius);
    glBindVertexArray(blurShader.uniformLocations[SHADER_SHADER_VAO]);
    glViewport(0, 0, halfW, halfH);
    glActiveTexture(GL_TEXTURE0);

    for (int iter = 0; iter < iterations; iter++) {
        // H pass: m_blurHalfFB → m_blurHalfTmpFB
        glBindFramebuffer(GL_FRAMEBUFFER, m_blurHalfTmpFB.getFBID());
        m_blurHalfFB.getTexture()->bind();
        glUniform2f(g_pGlobalState->locBlurDirection, 1.0f / halfW, 0.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // V pass: m_blurHalfTmpFB → m_blurHalfFB
        glBindFramebuffer(GL_FRAMEBUFFER, m_blurHalfFB.getFBID());
        m_blurHalfTmpFB.getTexture()->bind();
        glUniform2f(g_pGlobalState->locBlurDirection, 0.0f, 1.0f / halfH);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // Restore caller's GL state without querying (avoids pipeline stalls)
    glBindFramebuffer(GL_FRAMEBUFFER, callerFB);
    glBindVertexArray(0);
    glViewport(0, 0, viewportW, viewportH);
}

void CLiquidGlassDecoration::applyLiquidGlassEffect(CFramebuffer& sourceFB, CFramebuffer& targetFB,
                                                      CBox& rawBox, CBox& transformedBox, float windowAlpha) {
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

    const auto FULLSIZE = Vector2D(transformedBox.width, transformedBox.height);
    g_pGlobalState->shader.setUniformFloat2(SHADER_FULL_SIZE,
        static_cast<float>(FULLSIZE.x), static_cast<float>(FULLSIZE.y));

    glUniform1f(g_pGlobalState->locRefractionStrength, static_cast<float>(**PREFRACT));
    glUniform1f(g_pGlobalState->locChromaticAberration, static_cast<float>(**PCHROMATIC));
    glUniform1f(g_pGlobalState->locFresnelStrength, static_cast<float>(**PFRESNEL));
    glUniform1f(g_pGlobalState->locSpecularStrength, static_cast<float>(**PSPECULAR));
    glUniform1f(g_pGlobalState->locGlassOpacity, static_cast<float>(**POPACITY) * windowAlpha);
    glUniform1f(g_pGlobalState->locEdgeThickness, static_cast<float>(**PEDGE));

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

    static auto* const PBLUR       = (Hyprlang::FLOAT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:liquid-glass:blur_strength")->getDataStaticPtr();
    static auto* const PITERATIONS = (Hyprlang::INT* const*)HyprlandAPI::getConfigValue(PHANDLE, "plugin:liquid-glass:blur_iterations")->getDataStaticPtr();
    float blurRadius = static_cast<float>(**PBLUR) * 12.0f;
    int blurIterations = std::clamp(static_cast<int>(**PITERATIONS), 1, 5);
    int viewportW = static_cast<int>(g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.x);
    int viewportH = static_cast<int>(g_pHyprOpenGL->m_renderData.pMonitor->m_transformedSize.y);
    blurBackground(blurRadius, blurIterations, SOURCE->getFBID(), viewportW, viewportH);

    applyLiquidGlassEffect(m_blurHalfFB, *SOURCE, wlrbox, transformBox, a);
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
