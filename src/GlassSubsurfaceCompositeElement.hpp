#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <memory>

class CGlassSubsurfaceState;

// Post-surface element for "subsurface item glass". Mirrors
// CGlassLayerCompositeElement — see GlassSubsurfacePassElement.hpp for why the
// geometry is carried in m_data instead of recomputed here.
class CGlassSubsurfaceCompositeElement : public IPassElement {
  public:
    struct SData {
        std::shared_ptr<CGlassSubsurfaceState> state;
        CBox                                   itemLogicalBox; // same box CGlassSubsurfacePassElement::boundingBox() pads, so the
                                                                // two elements are discarded together (see boundingBox() below)
        CBox                                   rawBox;
        CBox                                   transformBox;
        CRegion                                transformedRegion; // ext-background-effect-v1 region, in transformBox's pixel space
        PHLMONITORREF                          monitor;
        float                                  alpha = 1.0f;
    };

    explicit CGlassSubsurfaceCompositeElement(SData data);
    ~CGlassSubsurfaceCompositeElement() override = default;

    std::vector<UP<IPassElement>> draw() override;
    [[nodiscard]] bool                needsLiveBlur() override;
    [[nodiscard]] bool                needsPrecomputeBlur() override;
    [[nodiscard]] std::optional<CBox> boundingBox() override;

    [[nodiscard]] const char*      passName() override { return "CGlassSubsurfaceCompositeElement"; }
    [[nodiscard]] ePassElementType type() override { return EK_CUSTOM; }

  private:
    SData m_data;
};
