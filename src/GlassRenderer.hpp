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

// Above half the smaller side the bezel already covers the whole glass, so a larger
// edge_thickness_px could only flatten the centre's falloff further.
inline constexpr float EDGE_THICKNESS_PX_MAX_FRACTION = 0.5f;

// Bezel width as a fraction of the glass shape's smaller side. edge_thickness_px is in
// logical pixels (scaled like bevel_size); when it is > 0 it replaces the relative
// edge_thickness, which is returned untouched otherwise.
[[nodiscard]] constexpr float effectiveEdgeThickness(float relative, float pixelsLogical, float monitorScale,
                                                     float minDimensionPhysical) noexcept {
    if (!(pixelsLogical > 0.0f) || !(minDimensionPhysical > 0.0f))
        return relative;
    const float scale = monitorScale > 0.0f ? monitorScale : 1.0f;
    return std::min(pixelsLogical * scale / minDimensionPhysical, EDGE_THICKNESS_PX_MAX_FRACTION);
}

static_assert(effectiveEdgeThickness(0.06f, 0.0f, 1.0f, 400.0f) == 0.06f);
static_assert(effectiveEdgeThickness(0.06f, 12.0f, 1.0f, 400.0f) == 0.03f);
static_assert(effectiveEdgeThickness(0.06f, 12.0f, 2.0f, 800.0f) == 0.03f);
static_assert(effectiveEdgeThickness(0.06f, 10000.0f, 1.0f, 400.0f) == EDGE_THICKNESS_PX_MAX_FRACTION);
static_assert(effectiveEdgeThickness(0.06f, -5.0f, 1.0f, 400.0f) == 0.06f);
static_assert(effectiveEdgeThickness(0.06f, 12.0f, 1.0f, 0.0f) == 0.06f);
static_assert(effectiveEdgeThickness(0.06f, 12.0f, 1.0f, -10.0f) == 0.06f);

// edge_height, logical px. Far past any useful wall: it only keeps the shader's
// products of height and slope finite.
inline constexpr float EDGE_HEIGHT_MAX = 500.0f;

// How far outside the glass the lens rim's reflection reads, framebuffer px. Inside
// SAMPLE_PADDING_PX, so it reads sampled backdrop, never the clamped texture border.
inline constexpr float LENS_REFLECTION_REACH_PX = 0.8f * SAMPLE_PADDING_PX;
static_assert(LENS_REFLECTION_REACH_PX > 0.0f && LENS_REFLECTION_REACH_PX < SAMPLE_PADDING_PX);

// Must match the `regionRects[64]` array size declared in Shaders.hpp. 64 lets a client trace a
// rounded shape to within a pixel or two; at 16 a capsule's curve steps by several pixels, and
// anything the client draws just outside the shape (a glow, a shadow) falls inside the steps.
inline constexpr int MAX_REGION_RECTS = 64;

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
    // Alpha mask only: glass fades in over this alpha range above alphaThreshold, so
    // antialiased edges fade instead of cutting off. 0 = hard mask.
    float    coverage       = 0.0f;

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

    // Subsurface items and shaped layers only: the glass shape's own sub-rect
    // within the drawn box, box-local pixels (see Shaders.hpp's glassBoxOffsetPx/SizePx).
    // Sentinel (negative size) means "use the full drawn box", applyGlassEffect's
    // old, unconditional behaviour — every other caller (windows, layers without
    // a shape) leaves this at the default and sees no change at all.
    Vector2D glassBoxOffsetPx{0.0, 0.0};
    Vector2D glassBoxSizePx{-1.0, -1.0};

    // mask_mode = contour layers only: the field of the outline the app draws (Contour.hpp).
    // Zero = none, the layer draws with its rectangle as every other layer. With one, the
    // glass shape (glassBox*) is ignored: the outline is the shape.
    GLuint   contourTextureId = 0;
    Vector2D contourUVScale{1.0, 1.0}; // box UV -> field UV
    float    contourReferencePx = 0.0f; // edge_thickness is a fraction of this
    float    contourReachPx     = 0.0f; // where the field stops
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

// True when every pixel of `box`, clipped to the framebuffer, lies inside `damage`: the
// frame redraws all of it. The only coverage predicate; sampleRegionCovered() asks it for
// the sample's source rectangle. `box` and `damage` are both in render space, which is the
// pixel space of every framebuffer the pass draws into: Hyprland renders it unrotated, so
// the monitor's transform never enters here.
[[nodiscard]] bool damageCoversBox(const CBox& box, const SP<Render::IFramebuffer>& framebuffer, const CRegion& damage);

// True when every pixel sampleBackground() would read for `box` lies inside
// `damage`. `box` is in framebuffer pixels, not the logical space boundingBox() pads in.
[[nodiscard]] bool sampleRegionCovered(const CBox& box, const SP<Render::IFramebuffer>& source, const CRegion& damage);

void sampleBackground(Render::CRenderContext& ctx, SP<Render::IFramebuffer>& sampleFramebuffer,
                       SP<Render::IFramebuffer> sourceFramebuffer, CBox box, Vector2D& outPaddingRatio, int downscale = 1);

// Draws the window's own committed surfaces over the sampled background, so the
// blur that follows works on a mix of the desktop and the window's own content.
// box is the same framebuffer-space box sampleBackground() was given.
void blendOwnContent(Render::CRenderContext& ctx, SP<Render::IFramebuffer>& sampleFramebuffer, PHLWINDOW window,
                      PHLMONITOR monitor, const CBox& box, int downscale, float amount, float cornerRadius, float roundingPower);

// callerFramebuffer is re-bound after the blur ping-pong; the viewport is
// restored from its size so it always matches the re-bound framebuffer.
// temporaryFramebuffer: the ping-pong target; null uses the one every glass shares. A caller blurring
// at another size than the rest passes its own, so the shared one is not reallocated back and forth.
void blurBackground(Render::CRenderContext& ctx, SP<Render::IFramebuffer> sampleFramebuffer, float radius,
                     int iterations, SP<Render::IFramebuffer> callerFramebuffer,
                     SP<Render::IFramebuffer>* temporaryFramebuffer = nullptr);

// The frosted copy of a window's sample (see Frost.hpp): the sample at half its size, blurred
// further by radius (in that smaller framebuffer's pixels). `copy` and `temporary` are the caller's,
// allocated here and reused while the size holds. callerFramebuffer is left bound, as blurBackground() does.
bool buildFrostCopy(Render::CRenderContext& ctx, const SP<Render::IFramebuffer>& sample, SP<Render::IFramebuffer>& copy,
                    SP<Render::IFramebuffer>& temporary, float radius, int iterations,
                    const SP<Render::IFramebuffer>& callerFramebuffer);

// What a window's glass draw needs for frost: the frosted copy and how much of it shows (0..1).
struct SFrostInput {
    SP<Render::IFramebuffer> copy;
    float                    amount = 0.0f;
};

// When mask is non-null (layers only), the shader composites the surface content
// over the glass effect in a single pass. When mask is null (windows), the shader
// outputs the glass effect alone.
//
// radii: per-corner radius (top-left, top-right, bottom-right, bottom-left).
// Windows and layers pass the same value four times — see CGlassDecoration::
// renderPass() and CGlassLayerSurface::compositeAndRestore(); only subsurface
// item glass (CGlassSubsurfaceState) ever passes unequal corners.
//
// focusLight: brightness of the light along the edge, 0..1 (focus_light times
// its fade). Only the active window's glass passes one above 0.
//
// frost: windows only, null when the glass is not frosted right now (see SFrostInput).
//
// box: the drawn quad in framebuffer pixels. The glass's up is the screen's up as the
// user sees it, whatever the monitor's transform.
void applyGlassEffect(Render::CRenderContext& ctx,
                       SP<Render::IFramebuffer> sampleFramebuffer, SP<Render::IFramebuffer> targetFramebuffer,
                       const CBox& box,
                       float alpha, const std::array<float, 4>& radii, float roundingPower,
                       const Vector2D& paddingRatio, const SResolveContext& resolveContext,
                       const SMaskInfo* mask = nullptr, float focusLight = 0.0f, const SFrostInput* frost = nullptr);

} // namespace GlassRenderer
