#include "GlassPassElement.hpp"
#include "GlassDecoration.hpp"
#include "Globals.hpp"
#include "WindowGeometry.hpp"

#include <hyprland/src/render/OpenGL.hpp>

CGlassPassElement::CGlassPassElement(const SGlassPassData& data)
    : m_data(data) {}

void CGlassPassElement::draw(const CRegion& damage) {
    if (!m_data.decoration)
        return;

    m_data.decoration->renderPass(g_pHyprOpenGL->m_renderData.pMonitor.lock(), m_data.alpha);
}

std::optional<CBox> CGlassPassElement::boundingBox() {
    if (!m_data.decoration)
        return std::nullopt;

    auto window = m_data.decoration->getOwner();
    if (!window)
        return std::nullopt;

    const auto monitor = g_pHyprOpenGL->m_renderData.pMonitor.lock();
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
    return m_data.decoration && m_data.decoration->needsResample();
}

bool CGlassPassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassPassElement::disableSimplification() {
    // The glass effect samples and blurs the full window background area.
    // Simplification would reduce our damage when opaque windows overlap,
    // causing partial blur, stale content, and visible seam artifacts.
    // Only needed when actively resampling; static frames reuse the cache.
    return m_data.decoration && m_data.decoration->needsResample();
}
