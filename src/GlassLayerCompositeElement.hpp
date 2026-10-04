#pragma once

#include "GlassLayerSurface.hpp"

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <memory>

class CGlassLayerCompositeElement : public IPassElement {
  public:
    struct SGlassLayerCompositeData {
        std::shared_ptr<CGlassLayerSurface> layerState;
        float                               alpha      = 1.0f;
        CGlassLayerSurface::EMaskSource      maskSource = CGlassLayerSurface::EMaskSource::ALPHA_THRESHOLD;
    };

    explicit CGlassLayerCompositeElement(const SGlassLayerCompositeData& data);
    ~CGlassLayerCompositeElement() override = default;

    std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override;
    [[nodiscard]] bool                needsLiveBlur(Render::CRenderContext&) override;
    [[nodiscard]] bool                needsPrecomputeBlur(Render::CRenderContext&) override;
    [[nodiscard]] std::optional<CBox> boundingBox(Render::CRenderContext& ctx) override;

    [[nodiscard]] const char* passName() override { return "CGlassLayerCompositeElement"; }
    [[nodiscard]] ePassElementType type() override { return EK_CUSTOM; }

  private:
    SGlassLayerCompositeData m_data;
};
