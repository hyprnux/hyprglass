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
    };

    explicit CGlassPassElement(const SGlassPassData& data);
    ~CGlassPassElement() override = default;

    std::vector<UP<IPassElement>> draw() override;
    [[nodiscard]] bool                needsLiveBlur() override;
    [[nodiscard]] bool                needsPrecomputeBlur() override;
    [[nodiscard]] std::optional<CBox> boundingBox() override;
    [[nodiscard]] bool                disableSimplification() override;
    void                               discard() override;

    [[nodiscard]] const char* passName() override { return "CGlassPassElement"; }
    [[nodiscard]] ePassElementType type() override { return EK_CUSTOM; }

  private:
    // Shared by boundingBox() and needsLiveBlur() so they can never disagree
    // about whether a box exists — see needsLiveBlur()'s comment.
    [[nodiscard]] std::optional<CBox> paddedLogicalBox() const;

    SGlassPassData m_data;
};
