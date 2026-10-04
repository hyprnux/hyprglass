#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <cstdint>

class CGlassDecoration;

class CGlassPassElement : public IPassElement {
  public:
    struct SGlassPassData {
        WP<CGlassDecoration> decoration;
        float                alpha = 1.0f;
        // Stamped in CGlassDecoration::queueGlassPass. 0 = pass we do not de-duplicate.
        uint64_t             frameSerial = 0;
        uint32_t             queueIndex  = 0;
        // Resolved once in draw(): needsLiveBlur() and renderPass() must agree on it.
        bool                 xray        = false;
    };

    explicit CGlassPassElement(const SGlassPassData& data);
    ~CGlassPassElement() override = default;

    std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override;
    [[nodiscard]] bool                needsLiveBlur(Render::CRenderContext& ctx) override;
    [[nodiscard]] bool                needsPrecomputeBlur(Render::CRenderContext&) override;
    [[nodiscard]] std::optional<CBox> boundingBox(Render::CRenderContext& ctx) override;
    [[nodiscard]] bool                disableSimplification(Render::CRenderContext&) override;
    void                               discard(Render::CRenderContext& ctx) override;

    [[nodiscard]] const char* passName() override { return "CGlassPassElement"; }
    [[nodiscard]] ePassElementType type() override { return EK_CUSTOM; }

  private:
    // Shared by boundingBox() and needsLiveBlur() so they can never disagree
    // about whether a box exists — see needsLiveBlur()'s comment.
    [[nodiscard]] std::optional<CBox> paddedLogicalBox(Render::CRenderContext& ctx) const;

    SGlassPassData m_data;
};
