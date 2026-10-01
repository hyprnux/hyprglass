#pragma once

#include <hyprland/src/render/Renderer.hpp>

// Shared by main.cpp's hkRenderLayer and CGlassDecoration::draw() — both need
// to tell a real frame's own render pass apart from a foreign one before
// touching any per-frame cache/fingerprint state.
namespace RenderGuards {

// A renderLayer/draw call that is not the monitor's own render pass: a caller
// rendering into its own framebuffer, or one that set a render modifier before
// calling us (only observable from inside pass execution — a modifier queued as
// a hints element is not applied yet while the pass is still being built).
// mainFB alone is not a usable guard: it is truthy for the whole span of a
// normal frame's begin()/end(), not just replays, so a bare mainFB check would
// never see a foreign render as foreign.
[[nodiscard]] inline bool isForeignRender() {
    const auto& renderData = g_pHyprRenderer->m_renderData;

    if (renderData.mainFB && renderData.currentFB != renderData.mainFB)
        return true;

    return renderData.renderModif.enabled && !renderData.renderModif.modifs.empty();
}

} // namespace RenderGuards
