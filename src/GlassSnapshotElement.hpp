#pragma once

#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/render/Renderer.hpp>

// X-ray: copies the frame's damaged region into the monitor's snapshot
// framebuffer. Added at RENDER_PRE_WINDOWS, so the snapshot holds the desktop
// with no window in it.
class CGlassSnapshotElement : public IPassElement {
  public:
    CGlassSnapshotElement()           = default;
    ~CGlassSnapshotElement() override = default;

    std::vector<UP<IPassElement>> draw() override;
    [[nodiscard]] bool            needsLiveBlur() override { return false; }
    [[nodiscard]] bool            needsPrecomputeBlur() override { return false; }
    [[nodiscard]] bool            undiscardable() override { return true; }

    [[nodiscard]] const char*      passName() override { return "CGlassSnapshotElement"; }
    [[nodiscard]] ePassElementType type() override { return EK_CUSTOM; }
};

// Marks the monitor's snapshot as still wanted, creating it on the first ask.
void                     requestXraySnapshot(PHLMONITOR monitor);

// Allocates or resizes the snapshot framebuffer to match the frame's, before
// the pass runs. False when there is nothing to copy into.
bool                     prepareXraySnapshot(PHLMONITOR monitor);

// The monitor's snapshot if it is filled and matches this frame, else nullptr.
SP<Render::IFramebuffer> xraySnapshotFor(PHLMONITOR monitor, const SP<Render::IFramebuffer>& frame);
