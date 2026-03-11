#include "GlassRenderer.hpp"
#include "BuiltInPresets.hpp"
#include "Globals.hpp"

#include <array>
#include <GLES3/gl32.h>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

namespace GlassRenderer {

static void uploadThemeUniforms(const SResolveContext& ctx) {
    const auto& uniforms = g_pGlobalState->shaderManager.glassUniforms;
    const auto& glassShader = g_pGlobalState->shaderManager.glassShader;
    const auto& defaults = ctx.isDark ? DARK_THEME_DEFAULTS : LIGHT_THEME_DEFAULTS;

    glassShader->setUniformFloat(SHADER_BRIGHTNESS, resolvePresetFloat(ctx, &SPresetValues::brightness, &SOverridableConfig::brightness, defaults.brightness));
    glassShader->setUniformFloat(SHADER_CONTRAST,   resolvePresetFloat(ctx, &SPresetValues::contrast, &SOverridableConfig::contrast, defaults.contrast));
    glUniform1f(uniforms.saturation,                 resolvePresetFloat(ctx, &SPresetValues::saturation, &SOverridableConfig::saturation, defaults.saturation));
    glassShader->setUniformFloat(SHADER_VIBRANCY,   resolvePresetFloat(ctx, &SPresetValues::vibrancy, &SOverridableConfig::vibrancy, defaults.vibrancy));
    glUniform1f(uniforms.vibrancyDarkness,           resolvePresetFloat(ctx, &SPresetValues::vibrancyDarkness, &SOverridableConfig::vibrancyDarkness, defaults.vibrancyDarkness));

    glUniform1f(uniforms.adaptiveDim,   resolvePresetFloat(ctx, &SPresetValues::adaptiveDim, &SOverridableConfig::adaptiveDim, defaults.adaptiveDim));
    glUniform1f(uniforms.adaptiveBoost, resolvePresetFloat(ctx, &SPresetValues::adaptiveBoost, &SOverridableConfig::adaptiveBoost, defaults.adaptiveBoost));
}

void sampleBackground(CFramebuffer& sampleFramebuffer, CFramebuffer& sourceFramebuffer,
                       CBox box, Vector2D& outPaddingRatio) {
    const int pad = SAMPLE_PADDING_PX;
    int paddedWidth  = static_cast<int>(box.width) + 2 * pad;
    int paddedHeight = static_cast<int>(box.height) + 2 * pad;

    if (sampleFramebuffer.m_size.x != paddedWidth || sampleFramebuffer.m_size.y != paddedHeight)
        sampleFramebuffer.alloc(paddedWidth, paddedHeight, sourceFramebuffer.m_drmFormat);

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

    outPaddingRatio = Vector2D(
        static_cast<double>(pad) / paddedWidth,
        static_cast<double>(pad) / paddedHeight
    );

    // The render pass scissors each element to its damage region.
    // That scissor state leaks here and clips glBlitFramebuffer on the
    // DRAW framebuffer, causing partial writes and stale noise artifacts.
    g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);

    // Clear the sample FBO before blitting. Clamped regions (near edges)
    // would otherwise contain uninitialized GPU memory (pink artifacts).
    glBindFramebuffer(GL_FRAMEBUFFER, sampleFramebuffer.getFBID());
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFramebuffer.getFBID());
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sampleFramebuffer.getFBID());
    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                      dstX0, dstY0, dstX1, dstY1,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void blurBackground(CFramebuffer& sampleFramebuffer, float radius, int iterations,
                    GLuint callerFramebufferID, int viewportWidth, int viewportHeight) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    if (radius <= 0.0f || iterations <= 0 || !shaderManager.isInitialized())
        return;

    int width  = static_cast<int>(sampleFramebuffer.m_size.x);
    int height = static_cast<int>(sampleFramebuffer.m_size.y);

    auto& blurTempFramebuffer = g_pGlobalState->blurTempFramebuffer;
    if (blurTempFramebuffer.m_size.x != width || blurTempFramebuffer.m_size.y != height)
        blurTempFramebuffer.alloc(width, height, sampleFramebuffer.m_drmFormat);

    // Fullscreen quad projection: maps VAO positions [0,1] to clip space [-1,1]
    static constexpr std::array<float, 9> FULLSCREEN_PROJECTION = {
        2.0f, 0.0f, 0.0f,
        0.0f, 2.0f, 0.0f,
       -1.0f,-1.0f, 1.0f,
    };

    const auto& blurUniforms = shaderManager.blurUniforms;

    auto shader = g_pHyprOpenGL->useShader(shaderManager.blurShader);
    shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, FULLSCREEN_PROJECTION);
    shader->setUniformInt(SHADER_TEX, 0);
    glUniform1f(blurUniforms.radius, radius);
    glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
    g_pHyprOpenGL->setViewport(0, 0, width, height);
    glActiveTexture(GL_TEXTURE0);

    // Ping-pong at full resolution: sampleFramebuffer ↔ blurTempFramebuffer
    for (int iteration = 0; iteration < iterations; iteration++) {
        // Horizontal pass: sampleFramebuffer → blurTempFramebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, blurTempFramebuffer.getFBID());
        sampleFramebuffer.getTexture()->bind();
        glUniform2f(blurUniforms.direction, 1.0f / width, 0.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Vertical pass: blurTempFramebuffer → sampleFramebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, sampleFramebuffer.getFBID());
        blurTempFramebuffer.getTexture()->bind();
        glUniform2f(blurUniforms.direction, 0.0f, 1.0f / height);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // Restore caller's GL state without querying (avoids pipeline stalls)
    glBindFramebuffer(GL_FRAMEBUFFER, callerFramebufferID);
    glBindVertexArray(0);
    g_pHyprOpenGL->setViewport(0, 0, viewportWidth, viewportHeight);
}

void applyGlassEffect(CFramebuffer& sampleFramebuffer, CFramebuffer& targetFramebuffer,
                       CBox& rawBox, CBox& transformedBox,
                       float alpha, float cornerRadius, float roundingPower,
                       const Vector2D& paddingRatio, const SResolveContext& resolveContext,
                       const SMaskInfo* mask) {
    auto& shaderManager  = g_pGlobalState->shaderManager;
    const auto& uniforms = shaderManager.glassUniforms;

    const auto transform = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprOpenGL->m_renderData.pMonitor->m_transform));

    Mat3x3 matrix   = g_pHyprOpenGL->m_renderData.monitorProjection.projectBox(rawBox, transform, rawBox.rot);
    Mat3x3 glMatrix = g_pHyprOpenGL->m_renderData.projection.copy().multiply(matrix);
    auto texture    = sampleFramebuffer.getTexture();

    glMatrix.transpose();

    glBindFramebuffer(GL_FRAMEBUFFER, targetFramebuffer.getFBID());
    glActiveTexture(GL_TEXTURE0);
    texture->bind();

    // Layers only: bind the temp FBO texture (rendered surface) on texture unit 1.
    // The shader samples it to mask glass to visible content and composite surface on top.
    // Windows pass mask=nullptr so this block is skipped.
    if (mask && mask->textureId != 0) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(mask->target, mask->textureId);
        glActiveTexture(GL_TEXTURE0);
    }

    auto shader = g_pHyprOpenGL->useShader(shaderManager.glassShader);

    shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, glMatrix.getMatrix());
    shader->setUniformInt(SHADER_TEX, 0);

    const auto fullSize = Vector2D(transformedBox.width, transformedBox.height);
    shader->setUniformFloat2(SHADER_FULL_SIZE,
        static_cast<float>(fullSize.x), static_cast<float>(fullSize.y));

    glUniform1f(uniforms.refractionStrength,  resolvePresetFloat(resolveContext, &SPresetValues::refractionStrength, &SOverridableConfig::refractionStrength));
    glUniform1f(uniforms.chromaticAberration, resolvePresetFloat(resolveContext, &SPresetValues::chromaticAberration, &SOverridableConfig::chromaticAberration));
    glUniform1f(uniforms.fresnelStrength,     resolvePresetFloat(resolveContext, &SPresetValues::fresnelStrength, &SOverridableConfig::fresnelStrength));
    glUniform1f(uniforms.specularStrength,    resolvePresetFloat(resolveContext, &SPresetValues::specularStrength, &SOverridableConfig::specularStrength));
    glUniform1f(uniforms.glassOpacity,        resolvePresetFloat(resolveContext, &SPresetValues::glassOpacity, &SOverridableConfig::glassOpacity) * alpha);
    glUniform1f(uniforms.edgeThickness,       resolvePresetFloat(resolveContext, &SPresetValues::edgeThickness, &SOverridableConfig::edgeThickness));
    glUniform1f(uniforms.lensDistortion,      resolvePresetFloat(resolveContext, &SPresetValues::lensDistortion, &SOverridableConfig::lensDistortion));

    uploadThemeUniforms(resolveContext);

    const int64_t tintColorValue = resolvePresetInt(resolveContext, &SPresetValues::tintColor, &SOverridableConfig::tintColor);
    glUniform3f(uniforms.tintColor,
        static_cast<float>((tintColorValue >> 24) & 0xFF) / 255.0f,
        static_cast<float>((tintColorValue >> 16) & 0xFF) / 255.0f,
        static_cast<float>((tintColorValue >> 8) & 0xFF) / 255.0f);
    glUniform1f(uniforms.tintAlpha,
        static_cast<float>(tintColorValue & 0xFF) / 255.0f);

    glUniform2f(uniforms.uvPadding,
        static_cast<float>(paddingRatio.x),
        static_cast<float>(paddingRatio.y));

    // Layers only: enable mask and provide UV mapping from the glass quad into
    // the monitor-sized temp FBO. Windows use useMask=0 (no masking).
    if (mask && mask->textureId != 0) {
        glUniform1i(uniforms.useMask, 1);
        glUniform1i(uniforms.maskTex, 1);
        glUniform2f(uniforms.maskUVOffset,
            static_cast<float>(mask->uvOffset.x),
            static_cast<float>(mask->uvOffset.y));
        glUniform2f(uniforms.maskUVScale,
            static_cast<float>(mask->uvScale.x),
            static_cast<float>(mask->uvScale.y));
    } else {
        glUniform1i(uniforms.useMask, 0);
    }

    shader->setUniformFloat(SHADER_RADIUS, cornerRadius);
    shader->setUniformFloat(SHADER_ROUNDING_POWER, roundingPower);

    glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
    g_pHyprOpenGL->scissor(rawBox);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    g_pHyprOpenGL->scissor(nullptr);
}

} // namespace GlassRenderer
