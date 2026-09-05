#pragma once

#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/render/Renderer.hpp>

// X-ray support. Copies the frame's damaged region into a per-monitor
// snapshot framebuffer. Added to the render pass at RENDER_PRE_WINDOWS, i.e.
// after the background and bottom layers (the wallpaper) and before any
// window, so the snapshot holds the desktop with no windows in it. Windows and
// layers with x-ray sample their glass from this snapshot instead of the live
// frame: the wallpaper shows through them, windows underneath never do.
class CGlassSnapshotElement : public IPassElement {
  public:
    CGlassSnapshotElement()           = default;
    ~CGlassSnapshotElement() override = default;

    std::vector<UP<IPassElement>> draw() override;
    [[nodiscard]] bool            needsLiveBlur() override { return false; }
    [[nodiscard]] bool            needsPrecomputeBlur() override { return false; }
    [[nodiscard]] bool            disableSimplification() override { return true; }
    [[nodiscard]] bool            undiscardable() override { return true; }

    [[nodiscard]] const char*      passName() override { return "CGlassSnapshotElement"; }
    [[nodiscard]] ePassElementType type() override { return EK_CUSTOM; }
};

// Have the monitor redrawn in full on the next frame, so a fresh snapshot is
// populated at once. Deferred to the event loop: this is called from inside
// the render pass, where the current frame's damage is already fixed.
inline void requestFullRedraw(PHLMONITOR monitor) {
    g_pEventLoopManager->doLater([weak = PHLMONITORREF{monitor}] {
        if (const auto m = weak.lock())
            g_pHyprRenderer->damageMonitor(m);
    });
}
