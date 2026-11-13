#pragma once

#include <hyprland/src/desktop/view/LayerSurface.hpp>
#include <hyprland/src/desktop/view/Popup.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprutils/math/Box.hpp>
#include <cmath>
#include <optional>

namespace LayerGeometry {

// A global-logical box in monitor-local framebuffer pixels.
[[nodiscard]] inline std::optional<CBox> toMonitorPixelBox(CBox box, PHLMONITOR monitor) {
    if (!monitor)
        return std::nullopt;

    box.translate(-monitor->m_position);
    box.scale(monitor->m_scale).round().noNegativeSize();

    if (!std::isfinite(box.x) || !std::isfinite(box.y) || !std::isfinite(box.w) || !std::isfinite(box.h) || box.w <= 0.0 || box.h <= 0.0)
        return std::nullopt;

    return box;
}

[[nodiscard]] inline std::optional<CBox> computeLayerBox(PHLLS layerSurface, PHLMONITOR monitor) {
    if (!layerSurface || !monitor)
        return std::nullopt;

    // Full animated layer geometry in monitor-local framebuffer pixels.
    return toMonitorPixelBox(CBox{layerSurface->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT),
                                  layerSurface->size(Desktop::View::IGeometric::GEOMETRIC_CURRENT)},
                             monitor);
}

// Global-logical box of one popup of this layer as Hyprland draws it in a monitor
// render: the popups branch of renderLayer() with shouldRenderPopup() outside a
// workspace scene, placed like its surface pass element.
[[nodiscard]] inline std::optional<CBox> computePopupGlobalBox(const PHLLS& layerSurface, const SP<Desktop::View::CPopup>& popup) {
    if (!layerSurface || !popup || !popup->mapped() || !popup->resource() || !popup->alphaNonZero() || !popup->acceptsInput() || !popup->wlSurface())
        return std::nullopt;

    const auto surface = popup->wlSurface()->resource();
    if (!surface || !surface->m_current.texture || surface->m_current.size.x < 1 || surface->m_current.size.y < 1)
        return std::nullopt;

    const auto position = layerSurface->position(Desktop::View::IGeometric::GEOMETRIC_CURRENT) + popup->coordsRelativeToParent();
    if (!std::isfinite(position.x) || !std::isfinite(position.y))
        return std::nullopt;

    return CBox{position, surface->m_current.size};
}

// A monitor-pixel box converted back to monitor-local LOGICAL coordinates (the
// IPassElement::boundingBox() contract) and padded, shared by both the
// pre-surface and post-surface pass elements so they can never disagree on
// where the glass box is.
[[nodiscard]] inline std::optional<CBox> toPaddedLogicalBox(std::optional<CBox> box, PHLMONITOR monitor, float paddingPx) {
    if (!box || !monitor)
        return std::nullopt;

    const float scale = monitor->m_scale > 0.0f ? monitor->m_scale : 1.0f;
    box->scale(1.0 / scale).expand(paddingPx / scale).noNegativeSize().round();
    if (!std::isfinite(box->x) || !std::isfinite(box->y) || !std::isfinite(box->w) || !std::isfinite(box->h) || box->w <= 0.0 || box->h <= 0.0)
        return std::nullopt;

    return box;
}

} // namespace LayerGeometry
