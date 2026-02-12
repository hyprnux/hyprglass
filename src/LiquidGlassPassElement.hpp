#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>

class CLiquidGlassDecoration;

class CLiquidGlassPassElement : public IPassElement {
  public:
    struct SLiquidGlassData {
        CLiquidGlassDecoration* deco = nullptr;
        float                   a    = 1.0f;
    };

    CLiquidGlassPassElement(const SLiquidGlassData& data);
    virtual ~CLiquidGlassPassElement() = default;

    virtual void                draw(const CRegion& damage);
    virtual bool                needsLiveBlur();
    virtual bool                needsPrecomputeBlur();
    virtual std::optional<CBox> boundingBox();
    virtual bool                disableSimplification();

    virtual const char*         passName() {
        return "CLiquidGlassPassElement";
    }

  private:
    SLiquidGlassData m_data;
};
