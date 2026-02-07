#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprutils/math/Box.hpp>

class CRectElement : public IPassElement {
  public:
    CRectElement(const CBox& box, const CHyprColor& color);
    virtual ~CRectElement() = default;

    virtual void                draw(const CRegion& damage) override;
    virtual bool                needsLiveBlur() override;
    virtual bool                needsPrecomputeBlur() override;
    virtual std::optional<CBox> boundingBox() override;

    virtual const char* passName() override {
        return "CRectElement";
    }

  private:
    CBox       m_box;
    CHyprColor m_color;
};
