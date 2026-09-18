#pragma once

#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/state/WorkspaceState.hpp>

// Shared by CGlassLayerSurface::sampleAndRedirect and
// CGlassDecoration::wantsBackgroundResample: a workspace slide or fade moves
// the whole scene behind a still glass surface without changing the surface's
// own geometry, so neither path's own move/resize check would ever see it.
namespace WorkspaceAnimation {

// Scans every workspace of the monitor, not its active/special pointers: the
// special pointer is already cleared while the old workspace animates away.
[[nodiscard]] inline bool anyWorkspaceAnimating(PHLMONITOR monitor) {
    for (const auto& ws : State::workspaceState()->workspaces()) {
        if (ws->m_monitor != monitor)
            continue;
        if (ws->m_renderOffset->isBeingAnimated() || ws->m_alpha->isBeingAnimated())
            return true;
    }

    return false;
}

} // namespace WorkspaceAnimation
