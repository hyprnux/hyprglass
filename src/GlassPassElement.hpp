#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>

class CGlassDecoration;

class CGlassPassElement : public IPassElement {
  public:
    struct SGlassPassData {
        CGlassDecoration* decoration = nullptr;
        float             alpha      = 1.0f;
    };

    explicit CGlassPassElement(const SGlassPassData& data);
    ~CGlassPassElement() override = default;

    void                draw(const CRegion& damage) override;
    [[nodiscard]] bool                needsLiveBlur() override;
    [[nodiscard]] bool                needsPrecomputeBlur() override;
    [[nodiscard]] std::optional<CBox> boundingBox() override;
    [[nodiscard]] bool                disableSimplification() override;

    [[nodiscard]] const char* passName() override { return "CGlassPassElement"; }

  private:
    SGlassPassData m_data;
};
