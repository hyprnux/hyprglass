#pragma once

#include "PluginConfig.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>

// Shared GL rendering pipeline used by both window decorations and layer surfaces.
// Callers own their sample framebuffers; these functions operate on passed-in state.
namespace GlassRenderer {

inline constexpr int SAMPLE_PADDING_PX = 60;

// Layers only: alpha mask from the temp FBO that captured the rendered surface.
// Constrains the glass effect to regions where the layer has visible content.
// Windows do not use masking, they pass mask=nullptr to applyGlassEffect.
struct SMaskInfo {
    GLuint   textureId;
    GLenum   target;
    Vector2D uvOffset; // mapping from glass box UV → full surface UV
    Vector2D uvScale;
};

void sampleBackground(CFramebuffer& sampleFramebuffer, CFramebuffer& sourceFramebuffer,
                       CBox box, Vector2D& outPaddingRatio);

void blurBackground(CFramebuffer& sampleFramebuffer, float radius, int iterations,
                    GLuint callerFramebufferID, int viewportWidth, int viewportHeight);

// When mask is non-null (layers only), the shader composites the surface content
// over the glass effect in a single pass. When mask is null (windows), the shader
// outputs the glass effect alone.
void applyGlassEffect(CFramebuffer& sampleFramebuffer, CFramebuffer& targetFramebuffer,
                       CBox& rawBox, CBox& transformedBox,
                       float alpha, float cornerRadius, float roundingPower,
                       const Vector2D& paddingRatio, const SResolveContext& resolveContext,
                       const SMaskInfo* mask = nullptr);

} // namespace GlassRenderer
