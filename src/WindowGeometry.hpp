#pragma once

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprutils/math/Box.hpp>
#include <optional>

namespace WindowGeometry {

[[nodiscard]] inline std::optional<CBox> computeWindowBox(PHLWINDOW window, PHLMONITOR monitor) {
    if (!window || !monitor)
        return std::nullopt;

    const auto workspace = window->m_workspace;
    const auto workspaceOffset = workspace && !window->m_pinned
        ? workspace->m_renderOffset->value()
        : Vector2D();

    auto box = window->getWindowMainSurfaceBox();
    box.translate(workspaceOffset);
    box.translate(-monitor->m_position + window->m_floatingOffset);
    box.scale(monitor->m_scale);
    box.round();
    return box;
}

} // namespace WindowGeometry
