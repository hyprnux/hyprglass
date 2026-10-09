#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <memory>

class CGlassSubsurfaceState;

// Pre-surface element for "subsurface item glass". Mirrors
// CGlassLayerPassElement, but the item's box is captured once by main.cpp's
// CRenderPass::add hook (from the CSurfacePassElement's own getTexBox()) and
// carried here directly, rather than recomputed from a long-lived surface
// object on every call — see GlassSubsurfaceState.hpp for why.
class CGlassSubsurfacePassElement : public IPassElement {
  public:
    struct SData {
        std::shared_ptr<CGlassSubsurfaceState> state;
        CBox                                   itemLogicalBox; // CSurfacePassElement::getTexBox(), monitor-local logical
        CBox                                   transformBox;   // post-rotation-transform pixel box; see SubsurfaceGeometry.hpp
        PHLMONITORREF                          monitor;
        float                                  alpha = 1.0f;
    };

    explicit CGlassSubsurfacePassElement(SData data);
    ~CGlassSubsurfacePassElement() override = default;

    std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override;
    [[nodiscard]] bool                needsLiveBlur(Render::CRenderContext& ctx) override;
    [[nodiscard]] bool                needsPrecomputeBlur(Render::CRenderContext&) override;
    [[nodiscard]] std::optional<CBox> boundingBox(Render::CRenderContext& ctx) override;
    [[nodiscard]] bool                disableSimplification(Render::CRenderContext&) override;

    [[nodiscard]] const char*      passName() override { return "CGlassSubsurfacePassElement"; }
    [[nodiscard]] ePassElementType type() override { return EK_CUSTOM; }

  private:
    SData m_data;
    // set once draw() redirected currentFB, which then differs from mainFB
    bool m_redirected = false;
};
