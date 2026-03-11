#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <memory>

class CGlassLayerSurface;

class CGlassLayerCompositeElement : public IPassElement {
  public:
    struct SGlassLayerCompositeData {
        std::shared_ptr<CGlassLayerSurface> layerState;
        float                               alpha = 1.0f;
    };

    explicit CGlassLayerCompositeElement(const SGlassLayerCompositeData& data);
    ~CGlassLayerCompositeElement() override = default;

    void                draw(const CRegion& damage) override;
    [[nodiscard]] bool                needsLiveBlur() override;
    [[nodiscard]] bool                needsPrecomputeBlur() override;
    [[nodiscard]] std::optional<CBox> boundingBox() override;

    [[nodiscard]] const char* passName() override { return "CGlassLayerCompositeElement"; }

  private:
    SGlassLayerCompositeData m_data;
};
