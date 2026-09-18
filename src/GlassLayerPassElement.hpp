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

    std::vector<UP<IPassElement>> draw() override;
    [[nodiscard]] bool                needsLiveBlur() override;
    [[nodiscard]] bool                needsPrecomputeBlur() override;
    [[nodiscard]] std::optional<CBox> boundingBox() override;
    [[nodiscard]] bool                disableSimplification() override;

    [[nodiscard]] const char* passName() override { return "CGlassLayerPassElement"; }
    [[nodiscard]] ePassElementType type() override { return EK_CUSTOM; }

  private:
    // Shared by boundingBox() and needsLiveBlur() so they can never disagree
    // about whether a box exists — see needsLiveBlur()'s comment.
    [[nodiscard]] std::optional<CBox> paddedLogicalBox() const;

    SGlassLayerPassData m_data;
};
