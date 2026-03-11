#pragma once

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprutils/math/Box.hpp>
#include <optional>

namespace LayerGeometry {

[[nodiscard]] inline std::optional<CBox> computeLayerBox(PHLLS layerSurface, PHLMONITOR monitor) {
    if (!layerSurface || !monitor)
        return std::nullopt;

    // Full animated layer geometry — the temp FBO mask constrains glass to visible
    // content pixel-perfectly, so input region subsetting is not needed here.
    auto box = CBox{layerSurface->m_realPosition->value(), layerSurface->m_realSize->value()};
    box.translate(-monitor->m_position);
    box.scale(monitor->m_scale);
    box.round();
    return box;
}

} // namespace LayerGeometry
