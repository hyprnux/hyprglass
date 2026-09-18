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

// Global-logical box (no monitor translate, no scale) — layout coordinates,
// not the monitor-local physical pixels computeWindowBox() returns. Shared by
// CGlassDecoration::damageEntire() (paddingPx = GlassRenderer::SAMPLE_PADDING_PX)
// and BackgroundDamageObserver's window overlap test, which must use the same
// coordinate family as the observer's damagedBox (also global-logical).
[[nodiscard]] inline std::optional<CBox> computePaddedGlobalBox(PHLWINDOW window, float paddingPx) {
    if (!window)
        return std::nullopt;

    const auto workspace = window->m_workspace;
    auto box = window->getWindowMainSurfaceBox();

    // A slide translates content under a still window with no geometry change
    // of its own; only relevant while the workspace is actually animating.
    if (workspace && workspace->m_renderOffset->isBeingAnimated() && !window->m_pinned)
        box.translate(workspace->m_renderOffset->value());
    box.translate(window->m_floatingOffset);

    const auto monitor = window->m_monitor.lock();
    const float scale = monitor ? monitor->m_scale : 1.0f;
    box.expand(paddingPx / scale);

    return box;
}

// The monitor transform CGlassDecoration::renderPass() applies to its own
// transformBox, factored out so CGlassPassElement::needsLiveBlur() can apply
// it identically to the box it evaluates wantsBackgroundResample() against —
// the two must agree on 90/270-degree-rotated monitors.
[[nodiscard]] inline CBox applyMonitorTransform(CBox box, PHLMONITOR monitor) {
    if (!monitor)
        return box;

    const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform));
    box.transform(transform, monitor->m_transformedSize.x, monitor->m_transformedSize.y);
    return box;
}

} // namespace WindowGeometry
