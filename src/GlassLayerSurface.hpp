#pragma once

#include "Contour.hpp"
#include "GlassRenderer.hpp"
#include "PluginConfig.hpp"

#include <chrono>
#include <optional>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Region.hpp>

class CGlassLayerSurface {
  public:
    explicit CGlassLayerSurface(PHLLS layerSurface);
    ~CGlassLayerSurface();

    // Where the glass mask for this layer comes from this frame. CONTOUR cuts like
    // ALPHA_THRESHOLD and also traces the outline for the edge (see Contour.hpp).
    enum class EMaskSource { ALPHA_THRESHOLD, PROTOCOL_REGION, CONTOUR, NONE };

    // Phase 1 (pre-surface): sample+blur background, redirect currentFB → temp FBO
    void sampleAndRedirect(Render::CRenderContext& ctx, PHLMONITOR monitor, float alpha, bool xray);

    // Resolved once per frame where the pass element is queued. Never for a
    // background or bottom layer: they render before the snapshot is taken.
    [[nodiscard]] bool resolveXray() const;

    // The x-ray snapshot holds everything this layer would sample.
    [[nodiscard]] bool xraySnapshotCovers(Render::CRenderContext& ctx, PHLMONITOR monitor) const;

    // Phase 2 (post-surface): restore currentFB, apply glass masked by temp FBO, blit surface
    void compositeAndRestore(Render::CRenderContext& ctx, PHLMONITOR monitor, float alpha, EMaskSource maskSource);

    void damageIfMoved();

    // Content below committed damage in our sample region — resample next frame
    void markBackgroundDirty();

    // Applies a mark markBackgroundDirty() owed under the rate limit once its
    // deadline passed, or re-arms the shared timer for it.
    void flushDeferredResample(std::chrono::steady_clock::time_point now);

    [[nodiscard]] bool liveResampleEnabled() const;

    // mask_mode = contour: the app committed new content, so the outline field is stale.
    // Rebuilt on the layer's next draw, at most every Contour::MIN_REBUILD_INTERVAL.
    void markContourContentChanged();

    // The field is held, so a commit of this layer's own content matters.
    [[nodiscard]] bool holdsContour() const { return m_contour.held(); }

    // A field build is waiting: the render pass must redraw the whole layer for it.
    [[nodiscard]] bool needsContourBuild() const { return m_contour.pending(); }

    // Frees the field (and the shared build targets with the last one): the layer is no
    // longer drawn with contour, or is gone.
    void releaseContour() { m_contour.release(); }

    // Decides ALPHA_THRESHOLD vs PROTOCOL_REGION vs NONE for this layer, based on
    // mask_mode and the root surface's ext-background-effect-v1 state.
    [[nodiscard]] EMaskSource resolveMaskSource() const;

    [[nodiscard]] PHLLS getLayerSurface() const;

    // PROTOCOL_REGION only: the blur region's bounding box in the same
    // monitor-pixel space as layerBox/regionRects (matches transformedBlurRegion()
    // below). layerBox is the caller's already-computed LayerGeometry::computeLayerBox()
    // result (both call sites have one in hand; avoids recomputing it here). Used to
    // shrink the sample blit box, never the composite draw or boundingBox() (both stay
    // full-layer — see GlassLayerSurface.cpp). Nullopt when the region is empty.
    [[nodiscard]] std::optional<CBox> regionBoundingBoxAbsolute(PHLMONITOR monitor, const CBox& layerBox) const;

    // PROTOCOL_REGION only: the blur region's bounding box in global-logical
    // coordinates — the family BackgroundDamageObserver's damagedBox uses (no
    // monitor scale/transform: logical space needs none). Deliberately a
    // separate coordinate family from regionBoundingBoxAbsolute() above; reusing
    // that monitor-pixel-space box against damagedBox would be silently wrong at
    // any scale != 1 or monitor position != (0,0).
    [[nodiscard]] std::optional<CBox> regionBoundingBoxGlobal() const;

  private:
    PHLLSREF     m_layerSurface;
    SP<Render::IFramebuffer> m_sampleFramebuffer;
    SP<Render::IFramebuffer> m_surfaceTempFramebuffer;
    Vector2D     m_samplePaddingRatio;
    bool         m_hasCachedSample = false;
    bool         m_backgroundDirty = false;
    std::chrono::steady_clock::time_point m_lastDirtyMark{};
    // A mark the rate limit held back: the last change of a burst must still land.
    std::optional<std::chrono::steady_clock::time_point> m_deferredResampleAt;

    // Set at the end of sampleAndRedirect() when currentFB was actually redirected
    // to the temp FBO this frame, cleared by compositeAndRestore() after it reads it.
    // Guards against compositing against a stale temp FBO when the render pass
    // discarded the pre-surface element (see disableSimplification() in
    // GlassLayerPassElement.cpp).
    bool         m_redirectedThisFrame = false;

    void damageSampleRegion();

    // The cached sample came from the x-ray snapshot: switching x-ray on or off
    // must not reuse a sample of the other.
    bool m_cachedFromSnapshot = false;

    // Track last position/size to detect movement and expand damage
    Vector2D     m_lastPosition;
    Vector2D     m_lastSize;

    // Scene generation at last blur — skip re-sampling when only the layer
    // surface content changed (e.g. clock tick) but the background didn't.
    uint64_t     m_lastSceneGeneration = 0;

    // Transformed blur region computed by sampleAndRedirect() this frame
    // (PROTOCOL_REGION only, empty otherwise), reused by compositeAndRestore()'s
    // regionRects mask upload so the scale/translate sequence doesn't
    // run twice per frame. compositeAndRestore() recomputes via
    // transformedBlurRegion() as a defensive fallback if this is empty when it
    // shouldn't be (mask source disagreeing between the two calls in one frame).
    CRegion      m_cachedTransformedRegion;

    // Region bounding box the cached sample texture was taken from (same
    // coordinate space as m_cachedTransformedRegion's extents; nullopt = the
    // texture covers the full layer box). Written only when sampleAndRedirect()
    // actually samples, never on a deferred or cache-hit frame, so it always
    // describes m_sampleFramebuffer. Reused by compositeAndRestore() to build the
    // sampleUVOffset/uvScale mask uniforms that reconcile sampleBackground()'s
    // region-shrunk sample box against the full-layer quad UV (see
    // toSampleBoxUV() in Shaders.hpp) — without this, sampleBlurred() would
    // index the (now smaller) sample texture as if it still covered the whole
    // layer, warping the visible blur. Also compared frame-to-frame in
    // sampleAndRedirect() to force a resample when the region itself moves or
    // resizes with no other invalidation trigger (a layer's own commit doesn't
    // mark itself dirty).
    std::optional<CBox> m_cachedRegionSampleBox;

    // Surface-local logical blur region intersected to the layer's own size,
    // then scaled/translated into layerBox's pixel space —
    // shared by regionBoundingBoxAbsolute() and compositeAndRestore()'s
    // regionRects upload so both use identical, proven math.
    [[nodiscard]] CRegion transformedBlurRegion(PHLMONITOR monitor, const CBox& layerBox) const;

    // Saved currentFB pointer, restored in compositeAndRestore
    SP<Render::IFramebuffer> m_savedCurrentFB;

    // mask_mode = contour: the traced outline, rebuilt when the content changes.
    Contour::CField m_contour;

    // Brings the field up to date for this draw (a build when one is due and this frame redraws
    // the whole layer) and points maskInfo at it. False: draw with the layer's rectangle, as every
    // other layer does: no field yet, a stale one for another size, or contour unavailable.
    [[nodiscard]] bool attachContour(Render::CRenderContext& ctx, PHLMONITOR monitor, const CBox& layerBox, float alpha,
                                     float maskThreshold, const SResolveContext& resolveContext, GlassRenderer::SMaskInfo& maskInfo);

    [[nodiscard]] bool           resolveThemeIsDark() const;
    [[nodiscard]] std::string    resolvePresetName() const;
    [[nodiscard]] ELayerMaskMode resolveMaskMode() const;
    // layers:namespace_shapes entry for this layer; nullopt = the whole layer box
    [[nodiscard]] std::optional<SLayerShape> resolveShape() const;
};
