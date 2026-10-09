#pragma once

#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/pass/PassElement.hpp>
#include <hyprland/src/render/Renderer.hpp>

// X-ray: copies the frame's damaged region into the monitor's snapshot
// framebuffer. Queued at RENDER_PRE_WINDOWS, so the snapshot holds the desktop
// with no window in it.
class CGlassSnapshotElement : public IPassElement {
  public:
    CGlassSnapshotElement()           = default;
    ~CGlassSnapshotElement() override = default;

    std::vector<UP<IPassElement>> draw() override;
    [[nodiscard]] bool            needsLiveBlur() override { return false; }
    [[nodiscard]] bool            needsPrecomputeBlur() override { return false; }

    [[nodiscard]] const char*      passName() override { return "CGlassSnapshotElement"; }
    [[nodiscard]] ePassElementType type() override { return EK_CUSTOM; }
};

// False under render:xp_mode, which draws no wallpaper or bottom layers to show.
[[nodiscard]] bool xrayHasBackground();

// Marks the monitor's snapshot as still wanted this frame, creating it on the
// first ask. Called while the pass is built, never from inside it.
void requestXraySnapshot(PHLMONITOR monitor);

// RENDER_BEGIN: advances the rendered monitor's frame count, drops a snapshot
// nothing asked for lately and cuts this frame's damage from its valid region.
void beginXraySnapshotFrame();

// RENDER_PRE_WINDOWS: sizes the snapshot to the frame and queues the copy.
void queueXraySnapshot();

// The monitor's snapshot when it matches `frame` and holds every pixel
// sampleBackground() reads for `box` (framebuffer pixels), else nullptr.
[[nodiscard]] SP<Render::IFramebuffer> xraySnapshotCovering(PHLMONITOR monitor, const SP<Render::IFramebuffer>& frame, const CBox& box);
