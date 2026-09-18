#pragma once

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprutils/math/Box.hpp>
#include <cmath>
#include <optional>

namespace LayerGeometry {

[[nodiscard]] inline std::optional<CBox> computeLayerBox(PHLLS layerSurface, PHLMONITOR monitor) {
    if (!layerSurface || !monitor)
        return std::nullopt;

    // Full animated layer geometry in monitor-local framebuffer pixels.
    auto box = CBox{layerSurface->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT),
                    layerSurface->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT)};
    box.translate(-monitor->m_position);
    box.scale(monitor->m_scale).round().noNegativeSize();

    if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.w) || !std::isfinite(box.h) || box.w <= 0.0 || box.h <= 0.0)
        return std::nullopt;

    return box;
}

// computeLayerBox() converted back to monitor-local LOGICAL coordinates (the
// IPassElement::boundingBox() contract) and padded, shared by both the
// pre-surface and post-surface pass elements so they can never disagree on
// where the layer's box is.
[[nodiscard]] inline std::optional<CBox> computePaddedLogicalLayerBox(PHLLS layerSurface, PHLMONITOR monitor, float paddingPx) {
    auto box = computeLayerBox(layerSurface, monitor);
    if (!box || !monitor)
        return std::nullopt;

    const float scale = monitor->m_scale > 0.0f ? monitor->m_scale : 1.0f;
    box->scale(1.0 / scale).expand(paddingPx / scale).noNegativeSize().round();
    if (!std::isfinite(box->x) || !std::isfinite(box->y) || !std::isfinite(box->w) || !std::isfinite(box->h) || box->w <= 0.0 || box->h <= 0.0)
        return std::nullopt;

    return box;
}

} // namespace LayerGeometry
