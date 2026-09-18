#include "GlassRenderer.hpp"
#include "BuiltInPresets.hpp"
#include "Diagnostics.hpp"
#include "Globals.hpp"

#include <algorithm>
#include <array>
#include <GLES3/gl32.h>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/pass/TexPassElement.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>

namespace GlassRenderer {

static GLuint fbId(const SP<Render::IFramebuffer>& framebuffer) {
    return dynamic_cast<Render::GL::CGLFramebuffer*>(framebuffer.get())->getFBID();
}

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

CBox SSampleMap::toSample(const CBox& framebufferBox) const {
    return CBox{(framebufferBox.x - srcX0) * scaleX, (framebufferBox.y - srcY0) * scaleY,
                framebufferBox.width * scaleX, framebufferBox.height * scaleY};
}

SSampleMap sampleMapFor(const CBox& box, int downscale) {
    SSampleMap map;
    map.fullWidth  = static_cast<int>(box.width) + 2 * SAMPLE_PADDING_PX;
    map.fullHeight = static_cast<int>(box.height) + 2 * SAMPLE_PADDING_PX;

    // Reduced resolution when blur is strong enough to hide it.
    // Weak blur at half-res shows pixelation.
    map.width  = std::max(1, map.fullWidth / downscale);
    map.height = std::max(1, map.fullHeight / downscale);

    map.srcX0  = static_cast<int>(box.x) - SAMPLE_PADDING_PX;
    map.srcY0  = static_cast<int>(box.y) - SAMPLE_PADDING_PX;
    map.srcX1  = static_cast<int>(box.x + box.width) + SAMPLE_PADDING_PX;
    map.srcY1  = static_cast<int>(box.y + box.height) + SAMPLE_PADDING_PX;
    map.scaleX = static_cast<float>(map.width) / map.fullWidth;
    map.scaleY = static_cast<float>(map.height) / map.fullHeight;
    return map;
}

SFoldedBlur foldBlurPasses(float radius, int iterations) noexcept {
    const float totalRadius      = radius * std::sqrt(static_cast<float>(iterations));
    const float cappedRatio      = totalRadius / BLUR_SHADER_TAP_CAP;
    const int   foldedIterations = std::max(1, static_cast<int>(std::ceil(cappedRatio * cappedRatio)));

    if (foldedIterations >= iterations)
        return {radius, iterations};

    return {totalRadius / std::sqrt(static_cast<float>(foldedIterations)), foldedIterations};
}

void sampleBackground(SP<Render::IFramebuffer>& sampleFramebuffer, SP<Render::IFramebuffer> sourceFramebuffer,
                       CBox box, Vector2D& outPaddingRatio, int downscale) {
    if (!sourceFramebuffer)
        return;

    Diagnostics::CScopedStageTimer stageTimer(Diagnostics::EStage::SampleBackground);

    const int  pad = SAMPLE_PADDING_PX;
    const auto map = sampleMapFor(box, downscale);

    int fullWidth  = map.fullWidth;
    int fullHeight = map.fullHeight;

    // Full-res source pixels this call blits, before any downscale — what the
    // GPU actually reads off the source framebuffer, not the (possibly
    // half-res) destination the sample FBO ends up holding.
    if (const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock())
        Diagnostics::recordSampledPixels(monitor->m_id, static_cast<double>(fullWidth) * static_cast<double>(fullHeight));

    int sampleWidth  = map.width;
    int sampleHeight = map.height;

    if (!sampleFramebuffer)
        sampleFramebuffer = g_pHyprRenderer->createFB("hyprglass-sample");

    // the format follows the monitor framebuffer, which changes with cm/bitdepth
    if (sampleFramebuffer->m_size.x != sampleWidth || sampleFramebuffer->m_size.y != sampleHeight ||
        sampleFramebuffer->m_drmFormat != sourceFramebuffer->m_drmFormat)
        sampleFramebuffer->alloc(sampleWidth, sampleHeight, sourceFramebuffer->m_drmFormat);

    int srcX0 = map.srcX0;
    int srcX1 = map.srcX1;
    int srcY0 = map.srcY0;
    int srcY1 = map.srcY1;

    // Clamp source coordinates to framebuffer bounds to avoid reading black/undefined pixels
    int framebufferWidth  = static_cast<int>(sourceFramebuffer->m_size.x);
    int framebufferHeight = static_cast<int>(sourceFramebuffer->m_size.y);

    // Destination coords in downscaled FBO space
    int dstX0 = 0, dstY0 = 0, dstX1 = sampleWidth, dstY1 = sampleHeight;

    // Scale destination adjustments proportionally for the downscaled FBO
    const float xScale = map.scaleX;
    const float yScale = map.scaleY;

    // Tracks whether any clamp shrank the destination rect below the full FBO,
    // which is the only case that can leave uninitialized texels after the blit.
    bool destinationClamped = false;

    if (srcX0 < 0) { dstX0 += static_cast<int>(-srcX0 * xScale); srcX0 = 0; destinationClamped = true; }
    if (srcY0 < 0) { dstY0 += static_cast<int>(-srcY0 * yScale); srcY0 = 0; destinationClamped = true; }
    if (srcX1 > framebufferWidth)  { dstX1 -= static_cast<int>((srcX1 - framebufferWidth) * xScale);  srcX1 = framebufferWidth; destinationClamped = true; }
    if (srcY1 > framebufferHeight) { dstY1 -= static_cast<int>((srcY1 - framebufferHeight) * yScale); srcY1 = framebufferHeight; destinationClamped = true; }

    // Padding ratio is relative to the logical content area (resolution-independent)
    outPaddingRatio = Vector2D(
        static_cast<double>(pad) / fullWidth,
        static_cast<double>(pad) / fullHeight
    );

    // The render pass scissors each element to its damage region.
    // That scissor state leaks here and clips glBlitFramebuffer on the
    // DRAW framebuffer, causing partial writes and stale noise artifacts.
    g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);

    // Clear the sample FBO before blitting only when the blit destination
    // doesn't cover the whole FBO. Clamped regions (near monitor edges)
    // would otherwise leave uninitialized GPU memory (pink artifacts) outside
    // the blit; a full-rect blit overwrites every texel, so the clear is redundant.
    if (destinationClamped) {
        glBindFramebuffer(GL_FRAMEBUFFER, fbId(sampleFramebuffer));
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbId(sourceFramebuffer));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbId(sampleFramebuffer));
    glBlitFramebuffer(srcX0, srcY0, srcX1, srcY1,
                      dstX0, dstY0, dstX1, dstY1,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);
}

void blendOwnContent(SP<Render::IFramebuffer>& sampleFramebuffer, PHLWINDOW window, PHLMONITOR monitor,
                      const CBox& box, int downscale, float amount, float cornerRadius, float roundingPower) {
    if (amount <= 0.0f || !sampleFramebuffer || !window || !monitor)
        return;

    // The sample framebuffer holds already-rotated framebuffer-space pixels, while a
    // texture drawn under RPT_EXPORT lands axis-aligned: on a 90/270 output the self
    // image would come out rotated. Skipping keeps those outputs at today's look.
    if (monitor->m_transform != WL_OUTPUT_TRANSFORM_NORMAL)
        return;

    // Hyprland renders a transformed window through a redirected pass and blits the
    // result; our untransformed copy would not match what the user sees.
    if (!window->m_transformers.empty())
        return;

    const auto hlSurface = window->wlSurface();
    const auto root      = hlSurface ? hlSurface->resource() : nullptr;
    if (!root)
        return;

    const auto     map    = sampleMapFor(box, downscale);
    const Vector2D fbSize = sampleFramebuffer->m_size;

    // The map only describes this framebuffer if sampleBackground() sized it from the
    // same box; otherwise boxes would project against one size and rasterise into another.
    if (fbSize.x != map.width || fbSize.y != map.height)
        return;

    auto& renderData = g_pHyprRenderer->m_renderData;

    // Leave the caller the state sampleBackground leaves: scissor off, viewport from
    // the re-bound framebuffer's own size (monitor sizes are wrong here, #41).
    // Declared before the FB guard so it runs after that framebuffer is back, on the
    // throwing path too.
    const Hyprutils::Utils::CScopeGuard restoreGLState([&] {
        g_pHyprOpenGL->scissor(nullptr);
        if (const auto& restored = renderData.currentFB)
            g_pHyprOpenGL->setViewport(0, 0, static_cast<int>(restored->m_size.x), static_cast<int>(restored->m_size.y));
    });

    auto guard = g_pHyprRenderer->bindTempFB(sampleFramebuffer);

    const auto  savedProjection      = renderData.projectionType;
    const auto  savedFbSize          = renderData.fbSize;
    const auto  savedRenderModif     = renderData.renderModif;
    const auto  savedWindow          = renderData.currentWindow;
    const auto  savedSurface         = renderData.surface;
    const auto  savedClipBox         = renderData.clipBox;
    const auto  savedUVTopLeft       = renderData.primarySurfaceUVTopLeft;
    const auto  savedUVBottomRight   = renderData.primarySurfaceUVBottomRight;
    const bool  savedTransformDamage = renderData.transformDamage;

    // draw() allocates a pass element per surface, so the restores must survive a
    // throw: every later element would otherwise project through the sample size.
    // Only render data, no GL state, so running after the FB rebind below is safe.
    const Hyprutils::Utils::CScopeGuard restoreRenderData([&] {
        renderData.primarySurfaceUVBottomRight = savedUVBottomRight;
        renderData.primarySurfaceUVTopLeft     = savedUVTopLeft;
        renderData.clipBox                     = savedClipBox;
        renderData.surface                     = savedSurface;
        renderData.currentWindow               = savedWindow;
        renderData.renderModif                 = savedRenderModif;
        renderData.transformDamage             = savedTransformDamage;
        // before setProjectionType: RPT_FB recomputes the projection from fbSize
        renderData.fbSize = savedFbSize;
        g_pHyprRenderer->setProjectionType(savedProjection);
    });

    renderData.fbSize = fbSize;
    g_pHyprRenderer->setProjectionType(Render::RPT_EXPORT);
    // our boxes and damage rects are sample-framebuffer pixels, not monitor space
    renderData.transformDamage = false;
    // an overview plugin's modifier would otherwise be applied a second time
    renderData.renderModif = {};
    // a monitor-space clip would scissor the injection to the wrong rectangle
    renderData.clipBox = {};
    // Only for the rgbx shader variant, the one thing the texture draw cannot be told
    // any other way. It also lets dim_inactive and the not-responding tint into the
    // self image of those windows: accepted, there is no allowDim on this draw path.
    if (window->m_ruleApplicator && window->m_ruleApplicator->RGBX().valueOrDefault())
        renderData.currentWindow = window;

    g_pHyprOpenGL->setViewport(0, 0, static_cast<int>(fbSize.x), static_cast<int>(fbSize.y));
    // Premultiplied source-over. Not restored afterwards: m_blend is private, and every
    // element boundary already leaves blending on with this same function.
    g_pHyprRenderer->blend(true);

    // Must be non-empty: renderTextureInternal drops empty damage, and an empty
    // region here makes Hyprland substitute its own, which is in monitor space.
    const CRegion fullDamage = CBox{0, 0, fbSize.x, fbSize.y};

    root->breadthfirst(
        [&](SP<CWLSurfaceResource> surface, const Vector2D& offset, void*) {
            const auto texture = surface->m_current.texture;
            // renderTextureInternal asserts on both, and an assert kills the compositor
            if (!texture || !texture->ok())
                return;
            if (surface->m_current.size.x < 1 || surface->m_current.size.y < 1)
                return;

            const bool mainSurface = surface == root;

            CBox       framebufferBox = box;
            if (!mainSurface) {
                framebufferBox = CBox{box.x + offset.x * monitor->m_scale, box.y + offset.y * monitor->m_scale,
                                      surface->m_current.size.x * monitor->m_scale, surface->m_current.size.y * monitor->m_scale};
                framebufferBox.round();
            }

            // Viewporter source crop, the one surface-state correction video players
            // need. Small and misaligned surfaces keep their uncorrected placement.
            // Each surface also blends at the same alpha, so where a subsurface covers
            // the main one the desktop share is (1-amount)^2: visible as a ghost of the
            // main surface at mid values, gone at 1.0. Fixing it needs a scratch target.
            const auto& viewport   = surface->m_current.viewport;
            const auto& bufferSize = surface->m_current.bufferSize;
            bool        customUV   = false;
            if (viewport.hasSource && bufferSize.x > 0 && bufferSize.y > 0) {
                const Vector2D uvTopLeft{viewport.source.x / bufferSize.x, viewport.source.y / bufferSize.y};
                const Vector2D uvBottomRight{(viewport.source.x + viewport.source.width) / bufferSize.x,
                                             (viewport.source.y + viewport.source.height) / bufferSize.y};
                if (uvBottomRight.x > 0.00001 && uvBottomRight.y > 0.00001) {
                    renderData.primarySurfaceUVTopLeft     = uvTopLeft;
                    renderData.primarySurfaceUVBottomRight = uvBottomRight;
                    customUV                               = true;
                }
            }
            if (!customUV) {
                renderData.primarySurfaceUVTopLeft     = Vector2D(-1, -1);
                renderData.primarySurfaceUVBottomRight = Vector2D(-1, -1);
            }

            g_pHyprRenderer->draw(CTexPassElement::SRenderData{
                .tex           = texture,
                .box           = map.toSample(framebufferBox),
                .a             = amount,
                .damage        = fullDamage,
                .round         = mainSurface ? static_cast<int>(cornerRadius * map.scaleX) : 0,
                .roundingPower = roundingPower,
                .allowCustomUV = customUV,
                .surface       = surface,
            });

            // The texture path tracks no buffer of its own, and the window's real draw
            // may be discarded as occluded, releasing the buffer we just read.
            if (surface->m_current.buffer && !surface->m_current.buffer->isSynchronous())
                g_pHyprRenderer->m_usedAsyncBuffers.emplace_back(surface->m_current.buffer);
        },
        nullptr);

    guard.reset();
}

void blurBackground(SP<Render::IFramebuffer> sampleFramebuffer, float radius, int iterations,
                    SP<Render::IFramebuffer> callerFramebuffer) {
    auto& shaderManager = g_pGlobalState->shaderManager;
    if (!sampleFramebuffer || !callerFramebuffer || radius <= 0.0f || iterations <= 0 || !shaderManager.isInitialized())
        return;

    Diagnostics::CScopedStageTimer stageTimer(Diagnostics::EStage::BlurBackground);

    // Two ping-pong passes (horizontal, vertical) per iteration.
    if (const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock())
        Diagnostics::recordBlurPasses(monitor->m_id, static_cast<uint64_t>(iterations) * 2);

    int width  = static_cast<int>(sampleFramebuffer->m_size.x);
    int height = static_cast<int>(sampleFramebuffer->m_size.y);

    auto& blurTempFramebuffer = g_pGlobalState->blurTempFramebuffer;
    if (!blurTempFramebuffer)
        blurTempFramebuffer = g_pHyprRenderer->createFB("hyprglass-blur-temp");

    if (blurTempFramebuffer->m_size.x != width || blurTempFramebuffer->m_size.y != height ||
        blurTempFramebuffer->m_drmFormat != sampleFramebuffer->m_drmFormat)
        blurTempFramebuffer->alloc(width, height, sampleFramebuffer->m_drmFormat);

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
        glBindFramebuffer(GL_FRAMEBUFFER, fbId(blurTempFramebuffer));
        sampleFramebuffer->getTexture()->bind();
        glUniform2f(blurUniforms.direction, 1.0f / width, 0.0f);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        // Vertical pass: blurTempFramebuffer → sampleFramebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, fbId(sampleFramebuffer));
        blurTempFramebuffer->getTexture()->bind();
        glUniform2f(blurUniforms.direction, 0.0f, 1.0f / height);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    // Restore caller's GL state without querying (avoids pipeline stalls).
    // The viewport must match the re-bound framebuffer's own size: monitor
    // sizes are wrong here on 90°/270° monitors, where m_transformedSize is
    // swapped relative to the framebuffer's native orientation (#41).
    glBindFramebuffer(GL_FRAMEBUFFER, fbId(callerFramebuffer));
    glBindVertexArray(0);
    g_pHyprOpenGL->setViewport(0, 0,
        static_cast<int>(callerFramebuffer->m_size.x),
        static_cast<int>(callerFramebuffer->m_size.y));
}

void applyGlassEffect(SP<Render::IFramebuffer> sampleFramebuffer, SP<Render::IFramebuffer> targetFramebuffer,
                       CBox& rawBox, CBox& transformedBox,
                       float alpha, float cornerRadius, float roundingPower,
                       const Vector2D& paddingRatio, const SResolveContext& resolveContext,
                       const SMaskInfo* mask) {
    if (!sampleFramebuffer || !targetFramebuffer)
        return;

    Diagnostics::CScopedStageTimer stageTimer(Diagnostics::EStage::ApplyGlassEffect);

    if (const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock())
        Diagnostics::recordGlassPixels(monitor->m_id, rawBox.w * rawBox.h);

    auto& shaderManager  = g_pGlobalState->shaderManager;
    const auto& uniforms = shaderManager.glassUniforms;

    const auto transform = Math::wlTransformToHyprutils(
        Math::invertTransform(g_pHyprRenderer->m_renderData.pMonitor->m_transform));

    Mat3x3 glMatrix = g_pHyprRenderer->projectBoxToTarget(rawBox, transform);
    auto texture    = sampleFramebuffer->getTexture();

    glMatrix.transpose();

    glBindFramebuffer(GL_FRAMEBUFFER, fbId(targetFramebuffer));
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

    // Reciprocals computed once per draw instead of once per pixel in the shader.
    const float minDimensionPx = static_cast<float>(std::min(fullSize.x, fullSize.y));
    glUniform2f(uniforms.invFullSize,
        1.0f / static_cast<float>(fullSize.x), 1.0f / static_cast<float>(fullSize.y));
    glUniform1f(uniforms.invRoundingPower, 1.0f / roundingPower);

    const float edgeThicknessValue = resolvePresetFloat(resolveContext, &SPresetValues::edgeThickness, &SOverridableConfig::edgeThickness);
    const float lensDistortionValue = resolvePresetFloat(resolveContext, &SPresetValues::lensDistortion, &SOverridableConfig::lensDistortion);
    // Epsilon guard matches the shader's own degenerate-size behaviour (bezelWidthPx == 0 would divide by zero).
    glUniform1f(uniforms.invBezelWidthPx, 1.0f / std::max(edgeThicknessValue * minDimensionPx, 1e-4f));
    glUniform1f(uniforms.lensMaxPx, lensDistortionValue * minDimensionPx * 0.006f);

    glUniform1f(uniforms.refractionStrength,  resolvePresetFloat(resolveContext, &SPresetValues::refractionStrength, &SOverridableConfig::refractionStrength));
    glUniform1f(uniforms.chromaticAberration, resolvePresetFloat(resolveContext, &SPresetValues::chromaticAberration, &SOverridableConfig::chromaticAberration));
    glUniform1f(uniforms.fresnelStrength,     resolvePresetFloat(resolveContext, &SPresetValues::fresnelStrength, &SOverridableConfig::fresnelStrength));
    glUniform1f(uniforms.specularStrength,    resolvePresetFloat(resolveContext, &SPresetValues::specularStrength, &SOverridableConfig::specularStrength));
    glUniform1f(uniforms.specularAngle,       resolvePresetFloat(resolveContext, &SPresetValues::specularAngle, &SOverridableConfig::specularAngle));
    glUniform1f(uniforms.glassOpacity,        resolvePresetFloat(resolveContext, &SPresetValues::glassOpacity, &SOverridableConfig::glassOpacity) * alpha);
    glUniform1f(uniforms.edgeThickness,       edgeThicknessValue);
    glUniform1f(uniforms.lensDistortion,      lensDistortionValue);
    glUniform1f(uniforms.refractionFlow,      resolvePresetFloat(resolveContext, &SPresetValues::refractionFlow, &SOverridableConfig::refractionFlow));
    glUniform1f(uniforms.refractionSpread,    resolvePresetFloat(resolveContext, &SPresetValues::refractionSpread, &SOverridableConfig::refractionSpread));
    glUniform1f(uniforms.fresnelTint,         resolvePresetFloat(resolveContext, &SPresetValues::fresnelTint, &SOverridableConfig::fresnelTint));
    glUniform1f(uniforms.bevelStrength,       resolvePresetFloat(resolveContext, &SPresetValues::bevelStrength, &SOverridableConfig::bevelStrength));
    glUniform1f(uniforms.bevelSize,           resolvePresetFloat(resolveContext, &SPresetValues::bevelSize, &SOverridableConfig::bevelSize));
    glUniform1f(uniforms.bevelTint,           resolvePresetFloat(resolveContext, &SPresetValues::bevelTint, &SOverridableConfig::bevelTint));
    glUniform1f(uniforms.bevelAngle,          resolvePresetFloat(resolveContext, &SPresetValues::bevelAngle, &SOverridableConfig::bevelAngle));
    glUniform1f(uniforms.bevelShadow,         resolvePresetFloat(resolveContext, &SPresetValues::bevelShadow, &SOverridableConfig::bevelShadow));

    // bevel width is defined in logical px; scale it to framebuffer px per monitor
    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    glUniform1f(uniforms.monitorScale, monitor && monitor->m_scale > 0.0f ? monitor->m_scale : 1.0f);

    uploadThemeUniforms(resolveContext);

    const int64_t tintColorValue = resolvePresetInt(resolveContext, &SPresetValues::tintColor, &SOverridableConfig::tintColor);
    glUniform3f(uniforms.tintColor,
        static_cast<float>((tintColorValue >> 24) & 0xFF) / 255.0f,
        static_cast<float>((tintColorValue >> 16) & 0xFF) / 255.0f,
        static_cast<float>((tintColorValue >> 8) & 0xFF) / 255.0f);
    glUniform1f(uniforms.tintAlpha,
        static_cast<float>(tintColorValue & 0xFF) / 255.0f);

    const int64_t fresnelColorValue = resolvePresetInt(resolveContext, &SPresetValues::fresnelColor, &SOverridableConfig::fresnelColor);
    glUniform3f(uniforms.fresnelColor,
        static_cast<float>((fresnelColorValue >> 24) & 0xFF) / 255.0f,
        static_cast<float>((fresnelColorValue >> 16) & 0xFF) / 255.0f,
        static_cast<float>((fresnelColorValue >> 8) & 0xFF) / 255.0f);
    glUniform1f(uniforms.fresnelColorAlpha,
        static_cast<float>(fresnelColorValue & 0xFF) / 255.0f);

    const int64_t bevelColorValue = resolvePresetInt(resolveContext, &SPresetValues::bevelColor, &SOverridableConfig::bevelColor);
    glUniform3f(uniforms.bevelColor,
        static_cast<float>((bevelColorValue >> 24) & 0xFF) / 255.0f,
        static_cast<float>((bevelColorValue >> 16) & 0xFF) / 255.0f,
        static_cast<float>((bevelColorValue >> 8) & 0xFF) / 255.0f);
    glUniform1f(uniforms.bevelColorAlpha,
        static_cast<float>(bevelColorValue & 0xFF) / 255.0f);

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
        glUniform1f(uniforms.maskAlphaThreshold, mask->alphaThreshold);
        glUniform1i(uniforms.maskMode, mask->maskMode);
        glUniform1i(uniforms.regionRectCount, mask->regionRectCount);
        if (mask->regionRectCount > 0)
            glUniform4fv(uniforms.regionRects, mask->regionRectCount,
                         reinterpret_cast<const float*>(mask->regionRects.data()));
        glUniform2f(uniforms.sampleUVOffset,
            static_cast<float>(mask->sampleUVOffset.x), static_cast<float>(mask->sampleUVOffset.y));
        glUniform2f(uniforms.sampleUVScale,
            static_cast<float>(mask->sampleUVScale.x), static_cast<float>(mask->sampleUVScale.y));
    } else {
        glUniform1i(uniforms.useMask, 0);
        glUniform1f(uniforms.maskAlphaThreshold, 0.001f);
        glUniform1i(uniforms.maskMode, 0);
        glUniform1i(uniforms.regionRectCount, 0);
        // Windows, and layers outside PROTOCOL_REGION, always sample the same
        // box they draw — identity, since this shader program's uniforms
        // persist across draws that don't set them (a prior region-mode
        // layer's non-identity value would otherwise leak into this draw).
        glUniform2f(uniforms.sampleUVOffset, 0.0f, 0.0f);
        glUniform2f(uniforms.sampleUVScale, 1.0f, 1.0f);
    }

    shader->setUniformFloat(SHADER_RADIUS, cornerRadius);
    shader->setUniformFloat(SHADER_ROUNDING_POWER, roundingPower);

    glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));

    // Only finalDamage is copied to the screen, and elementDamage (which finalDamage is a
    // subset of) already covers every pixel we're visible at, so scissoring to the damage
    // clips no pixel that would otherwise reach the screen.
    CBox damageExtents = g_pHyprRenderer->m_renderData.damage.copy().intersect(rawBox).getExtents();
    g_pHyprOpenGL->scissor(damageExtents.w > 0 && damageExtents.h > 0 ? damageExtents : rawBox);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    g_pHyprOpenGL->scissor(nullptr);
}

} // namespace GlassRenderer
