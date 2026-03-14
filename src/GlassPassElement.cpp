#include "GlassPassElement.hpp"
#include "GlassDecoration.hpp"
#include "Globals.hpp"
#include "WindowGeometry.hpp"

#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>

CGlassPassElement::CGlassPassElement(const SGlassPassData& data)
    : m_data(data) {}

std::optional<CBox> CGlassPassElement::boundingBox() {
    if (!m_data.decoration)
        return std::nullopt;

    auto window = m_data.decoration->getOwner();
    if (!window)
        return std::nullopt;

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    auto box = WindowGeometry::computeWindowBox(window, monitor);
    if (!box)
        return std::nullopt;

    // Expand by our sampling padding so the render pass damages the full
    // area we read from. Without this, wallpaper outside the window box
    // but inside our padding isn't re-rendered, leaving stale content.
    const float padding = CGlassDecoration::SAMPLE_PADDING_PX / monitor->m_scale;
    box->expand(padding);
    return box;
}

bool CGlassPassElement::needsLiveBlur() {
    return true;
}

bool CGlassPassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassPassElement::disableSimplification() {
    return true;
}
