#pragma once

#include "GlassRenderer.hpp"
#include "PluginConfig.hpp"

#include <chrono>
#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

class CGlassLayerSurface {
  public:
    explicit CGlassLayerSurface(PHLLS layerSurface);
    ~CGlassLayerSurface();

    // Where the glass mask for this layer comes from this frame.
    enum class EMaskSource { ALPHA_THRESHOLD, PROTOCOL_REGION, NONE };

    // Phase 1 (pre-surface): sample+blur background, redirect currentFB → temp FBO
    void sampleAndRedirect(PHLMONITOR monitor, float alpha);

    // Phase 2 (post-surface): restore currentFB, apply glass masked by temp FBO, blit surface
    void compositeAndRestore(PHLMONITOR monitor, float alpha, EMaskSource maskSource);

    void damageIfMoved();

    // Content below committed damage in our sample region — resample next frame
    void markBackgroundDirty();

    [[nodiscard]] bool liveResampleEnabled() const;

    // Decides ALPHA_THRESHOLD vs PROTOCOL_REGION vs NONE for this layer, based on
    // mask_mode and the root surface's ext-background-effect-v1 state.
    [[nodiscard]] EMaskSource resolveMaskSource() const;

    [[nodiscard]] PHLLS getLayerSurface() const;

  private:
    PHLLSREF     m_layerSurface;
    SP<Render::IFramebuffer> m_sampleFramebuffer;
    SP<Render::IFramebuffer> m_surfaceTempFramebuffer;
    Vector2D     m_samplePaddingRatio;
    bool         m_hasCachedSample = false;
    bool         m_backgroundDirty = false;
    std::chrono::steady_clock::time_point m_lastDirtyMark{};

    void damageSampleRegion();
    bool         m_cachedFromSnapshot = false; // the cached sample came from the x-ray snapshot

    // Track last position/size to detect movement and expand damage
    Vector2D     m_lastPosition;
    Vector2D     m_lastSize;

    // Scene generation at last blur — skip re-sampling when only the layer
    // surface content changed (e.g. clock tick) but the background didn't.
    uint64_t     m_lastSceneGeneration = 0;

    // Saved currentFB pointer, restored in compositeAndRestore
    SP<Render::IFramebuffer> m_savedCurrentFB;

    [[nodiscard]] bool           resolveThemeIsDark() const;
    [[nodiscard]] std::string    resolvePresetName() const;
    [[nodiscard]] ELayerMaskMode resolveMaskMode() const;
    [[nodiscard]] bool           resolveXray() const;
};
