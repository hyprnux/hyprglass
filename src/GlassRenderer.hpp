#pragma once

#include "PluginConfig.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <GLES3/gl32.h>
#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <hyprutils/math/Vector2D.hpp>

// Shared GL rendering pipeline used by both window decorations and layer surfaces.
// Callers own their sample framebuffers; these functions operate on passed-in state.
namespace GlassRenderer {

inline constexpr int SAMPLE_PADDING_PX = 60;

// Maximum downscale factor for blur sampling. Half-res (2) is 4x cheaper
// per blur pass. Only applied when blur is strong enough to hide the lower
// resolution — weak blur at half-res shows visible pixelation.
inline constexpr int   BLUR_DOWNSCALE_MAX       = 2;
inline constexpr float BLUR_DOWNSCALE_THRESHOLD = 0.35f; // min blur_strength for downscale

// Tap cap in gaussianblur.frag's `min(int(ceil(blurRadius)), 8)` (Shaders.hpp) — the
// per-pass radius where taps stop tracking radius 1:1 and the shader's designed ~3 sigma
// coverage starts truncating. Named here so foldBlurPasses() cannot drift from the shader's cap.
inline constexpr float BLUR_SHADER_TAP_CAP = 8.0f;

// Result of folding N blur passes at a fixed radius into fewer, larger-radius passes.
// See foldBlurPasses().
struct SFoldedBlur {
    float radius;
    int   iterations;
};

// Gaussian semigroup identity: N passes at `radius` produce the same total blur as one pass
// at `radius * sqrt(N)`, so N passes of sigma compound rather than average (blurBackground()
// reuses the same radius every pass). Finds the smallest pass count N' whose own per-pass
// radius (r_total / sqrt(N')) stays at or under the shader's tap cap, preserving the
// requested total blur while cutting fetches. Only ever returns fewer passes than requested:
// when the derived N' would be >= the input iterations (the fold would cost the same amount
// of work or more — e.g. a wide single-pass radius needing many passes to stay untruncated),
// the input is returned unchanged.
[[nodiscard]] SFoldedBlur foldBlurPasses(float radius, int iterations) noexcept;

// Must match the `regionRects[16]` array size declared in Shaders.hpp.
inline constexpr int MAX_REGION_RECTS = 16;

// Box-local pixel rect uploaded to the shader's regionRects uniform array.
struct SRegionRect {
    float x = 0, y = 0, w = 0, h = 0;
};
static_assert(sizeof(SRegionRect) == 4 * sizeof(float));

// Layers only: alpha mask from the temp FBO that captured the rendered surface.
// Constrains the glass effect to regions where the layer has visible content.
// Windows do not use masking, they pass mask=nullptr to applyGlassEffect.
struct SMaskInfo {
    GLuint   textureId;
    GLenum   target;
    Vector2D uvOffset; // mapping from glass box UV → full surface UV
    Vector2D uvScale;
    float    alphaThreshold = 0.001f;

    // 0 = alpha-threshold mask, 1 = ext-background-effect-v1 protocol region
    int                                        maskMode        = 0;
    std::array<SRegionRect, MAX_REGION_RECTS>  regionRects{};
    int                                        regionRectCount = 0;

    // Maps the glass quad's own UV (spanning the full drawn box) into the
    // sample texture's own normalized space, applied before uvPadding
    // (toSampleBoxUV() in Shaders.hpp). Identity (no-op) unless
    // sampleBackground() was given a box smaller than the drawn quad —
    // PROTOCOL_REGION layers only; see GlassLayerSurface.cpp.
    Vector2D sampleUVOffset{0.0, 0.0};
    Vector2D sampleUVScale{1.0, 1.0};
};

// Affine map from source-framebuffer pixels into the sample framebuffer.
// sampleBackground() blits through it and blendOwnContent() draws through it;
// both derive every coordinate from here so the two can never drift apart.
struct SSampleMap {
    int   fullWidth = 1, fullHeight = 1;              // padded box, framebuffer pixels
    int   width = 1, height = 1;                      // sample framebuffer size
    int   srcX0 = 0, srcY0 = 0, srcX1 = 1, srcY1 = 1; // padded box in source pixels, (srcX0, srcY0) lands on sample (0, 0)
    float scaleX = 1, scaleY = 1;

    [[nodiscard]] CBox toSample(const CBox& framebufferBox) const;
};

[[nodiscard]] SSampleMap sampleMapFor(const CBox& box, int downscale);

// True when every pixel sampleBackground() would read for `box` lies inside
// `damage`. The only coverage predicate. `box` is in post-transform framebuffer
// pixels like `damage`, not the logical space boundingBox() pads in.
[[nodiscard]] bool sampleRegionCovered(const CBox& box, const SP<Render::IFramebuffer>& source, const CRegion& damage);

void sampleBackground(SP<Render::IFramebuffer>& sampleFramebuffer, SP<Render::IFramebuffer> sourceFramebuffer,
                       CBox box, Vector2D& outPaddingRatio, int downscale = 1);

// Draws the window's own committed surfaces over the sampled background, so the
// blur that follows works on a mix of the desktop and the window's own content.
// box is the same framebuffer-space box sampleBackground() was given.
void blendOwnContent(SP<Render::IFramebuffer>& sampleFramebuffer, PHLWINDOW window, PHLMONITOR monitor,
                      const CBox& box, int downscale, float amount, float cornerRadius, float roundingPower);

// callerFramebuffer is re-bound after the blur ping-pong; the viewport is
// restored from its size so it always matches the re-bound framebuffer
// (monitor fields would be wrong on 90°/270° transformed monitors).
void blurBackground(SP<Render::IFramebuffer> sampleFramebuffer, float radius, int iterations,
                    SP<Render::IFramebuffer> callerFramebuffer);

// When mask is non-null (layers only), the shader composites the surface content
// over the glass effect in a single pass. When mask is null (windows), the shader
// outputs the glass effect alone.
void applyGlassEffect(SP<Render::IFramebuffer> sampleFramebuffer, SP<Render::IFramebuffer> targetFramebuffer,
                       CBox& rawBox, CBox& transformedBox,
                       float alpha, float cornerRadius, float roundingPower,
                       const Vector2D& paddingRatio, const SResolveContext& resolveContext,
                       const SMaskInfo* mask = nullptr);

} // namespace GlassRenderer
