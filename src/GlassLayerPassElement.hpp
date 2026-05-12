#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <memory>

class CGlassLayerSurface;

class CGlassLayerPassElement : public IPassElement {
  public:
    struct SGlassLayerPassData {
        std::shared_ptr<CGlassLayerSurface> layerState;
        float                               alpha = 1.0f;
    };

    explicit CGlassLayerPassElement(const SGlassLayerPassData& data);
    ~CGlassLayerPassElement() override = default;

    [[nodiscard]] bool                needsLiveBlur() override;
    [[nodiscard]] bool                needsPrecomputeBlur() override;
    [[nodiscard]] std::optional<CBox> boundingBox() override;
    [[nodiscard]] bool                disableSimplification() override;

    [[nodiscard]] const char* passName() override { return "CGlassLayerPassElement"; }

    virtual std::vector<UP<IPassElement>> draw() override;

    virtual ePassElementType type() override {
        return EK_CUSTOM;
    };

  private:
    SGlassLayerPassData m_data;
};
