#pragma once

#include "GlassRenderer.hpp"
#include "PluginConfig.hpp"

#include <hyprland/src/desktop/DesktopTypes.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Region.hpp>

// Per-item glass state for "subsurface item glass": one instance per glassed
// wl_subsurface, mirroring CGlassLayerSurface's two-phase temp-FBO pipeline
// but simplified for the subsurface case:
//   - the mask is always the ext-background-effect-v1 protocol region (the
//     hook only ever creates this state for a surface that has one — see
//     main.cpp's CRenderPass::add hook), never alpha-threshold;
//   - the sample box always equals the full item box (no region-shrink
//     optimization), so the mask's sample UV mapping is always identity;
//   - geometry (the item's box, in every coordinate family) is handed in by
//     the caller every frame rather than recomputed from a long-lived
//     surface object: CSurfacePassElement is transient and owned by the
//     render pass once enqueued, so the box is captured once at
//     CRenderPass::add() hook time and threaded through the pre/post
//     elements' own data (see GlassSubsurfacePassElement/CompositeElement);
//   - the item's own rendered-surface redirect target is one of
//     g_pGlobalState->subsurfaceTempFramebuffers, keyed by the item's
//     monitor and shared/reused serially by every glassed item on that
//     monitor in the frame (not owned per-instance here) — see Globals.hpp
//     for why that's safe.
// Preset hint as seen by one draw: what the client asked for, and whether it
// named no known preset so the chain fell through.
struct SPresetHintOutcome {
    std::string requested; // empty = no hint
    bool        rejected = false;
};

class CGlassSubsurfaceState {
  public:
    explicit CGlassSubsurfaceState(WP<CWLSurfaceResource> surface, PHLWINDOWREF window);
    ~CGlassSubsurfaceState();

    // Phase 1 (pre-surface): sample+blur background under transformBox, redirect
    // currentFB → temp FBO so the original CSurfacePassElement draw (called by
    // main.cpp's hook between this and compositeAndRestore) lands there instead.
    void sampleAndRedirect(PHLMONITOR monitor, const CBox& transformBox, float alpha);

    // Phase 2 (post-surface): restore currentFB, composite glass masked by the
    // protocol region with the temp FBO's content (the item's own foreground) on top.
    // transformedRegion is non-const: CRegion::getExtents() is a non-const method.
    void compositeAndRestore(PHLMONITOR monitor, const CBox& rawBox, const CBox& transformBox,
                              CRegion& transformedRegion, float alpha);

    [[nodiscard]] bool alive() const { return !m_surface.expired(); }

    // Diagnostic accessors for `hyprctl hyprglass items` (Diagnostics.cpp). All
    // reflect values already computed by the most recent compositeAndRestore()
    // call; hasDrawnOnce() false means the item has never composited and every
    // other accessor below still holds its default.
    [[nodiscard]] PHLWINDOWREF window() const { return m_window; }
    [[nodiscard]] bool         hasDrawnOnce() const { return m_hasDrawnOnce; }
    [[nodiscard]] const std::string& lastMonitorName() const { return m_lastMonitorName; }
    [[nodiscard]] const CBox&        lastGlassBox() const { return m_lastGlassBox; }
    [[nodiscard]] const std::array<float, 4>& lastRadii() const { return m_lastRadii; }
    [[nodiscard]] float              lastRoundingPower() const { return m_lastRoundingPower; }
    [[nodiscard]] const std::string& lastResolvedPreset() const { return m_lastResolvedPreset; }
    [[nodiscard]] const SPresetHintOutcome&  lastPresetHint() const { return m_lastPresetHint; }

  private:
    WP<CWLSurfaceResource>   m_surface;
    PHLWINDOWREF             m_window;
    SP<Render::IFramebuffer> m_sampleFramebuffer; // box-sized, per-item (small)
    Vector2D                 m_samplePaddingRatio;
    bool                     m_hasCachedSample = false;

    // Last transformBox seen, to force a resample when the item itself moves
    // or resizes with no other invalidation trigger (mirrors
    // CGlassLayerSurface::damageIfMoved(), inlined here since we have no
    // standing surface object to hang a separate call off of).
    CBox m_lastTransformBox;

    uint64_t m_lastSceneGeneration = 0;

    // Set at the end of sampleAndRedirect() when currentFB was actually
    // redirected this frame; cleared by compositeAndRestore() after it reads
    // it. Same discard-safety net as CGlassLayerSurface::m_redirectedThisFrame.
    bool m_redirectedThisFrame = false;

    // Saved currentFB pointer, restored in compositeAndRestore()
    SP<Render::IFramebuffer> m_savedCurrentFB;

    // Set at the end of compositeAndRestore() for `hyprctl hyprglass items`
    // (see the public accessors above); never read on the render path itself.
    bool                  m_hasDrawnOnce = false;
    std::string           m_lastMonitorName;
    CBox                  m_lastGlassBox;
    std::array<float, 4>  m_lastRadii{};
    float                 m_lastRoundingPower = 2.0f;
    std::string           m_lastResolvedPreset;
    SPresetHintOutcome    m_lastPresetHint;

    [[nodiscard]] bool        resolveThemeIsDark() const;
    [[nodiscard]] std::string resolvePresetName(SPresetHintOutcome* hintOutcome = nullptr) const;
};
