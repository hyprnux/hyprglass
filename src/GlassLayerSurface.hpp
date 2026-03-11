#pragma once

#include "GlassRenderer.hpp"
#include "PluginConfig.hpp"

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/render/Framebuffer.hpp>

class CGlassLayerSurface {
  public:
    explicit CGlassLayerSurface(PHLLS layerSurface);
    ~CGlassLayerSurface() = default;

    // Phase 1 (pre-surface): sample+blur background, redirect currentFB → temp FBO
    void sampleAndRedirect(PHLMONITOR monitor, float alpha);

    // Phase 2 (post-surface): restore currentFB, apply glass masked by temp FBO, blit surface
    void compositeAndRestore(PHLMONITOR monitor, float alpha);

    void damageIfMoved();

    [[nodiscard]] PHLLS getLayerSurface() const;

  private:
    PHLLSREF     m_layerSurface;
    CFramebuffer m_sampleFramebuffer;
    CFramebuffer m_surfaceTempFramebuffer;
    Vector2D     m_samplePaddingRatio;
    bool         m_hasCachedSample = false;

    // Saved currentFB pointer, restored in compositeAndRestore
    CFramebuffer* m_savedCurrentFB = nullptr;

    [[nodiscard]] bool        resolveThemeIsDark() const;
    [[nodiscard]] std::string resolvePresetName() const;
};
