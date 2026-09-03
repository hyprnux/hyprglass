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

    std::vector<UP<IPassElement>> draw() override;
    [[nodiscard]] bool                needsLiveBlur() override;
    [[nodiscard]] bool                needsPrecomputeBlur() override;
    [[nodiscard]] std::optional<CBox> boundingBox() override;

    [[nodiscard]] const char* passName() override { return "CGlassLayerCompositeElement"; }
    [[nodiscard]] ePassElementType type() override { return EK_CUSTOM; }

  private:
    SGlassLayerCompositeData m_data;
};
