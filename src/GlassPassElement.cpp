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
    return WindowGeometry::computeWindowBox(window, monitor);
}

bool CGlassPassElement::needsLiveBlur() {
    return true;
}

bool CGlassPassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassPassElement::disableSimplification() {
    // The glass effect samples and blurs the full window background area.
    // Simplification would reduce our damage when opaque windows overlap,
    // causing partial blur, stale content, and visible seam artifacts.
    return true;
}
