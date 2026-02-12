#include "LiquidGlassPassElement.hpp"
#include "LiquidGlassDecoration.hpp"
#include "globals.hpp"

#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/render/OpenGL.hpp>

CLiquidGlassPassElement::CLiquidGlassPassElement(const SLiquidGlassData& data)
    : m_data(data) {}

void CLiquidGlassPassElement::draw(const CRegion& damage) {
    if (!m_data.deco)
        return;

    m_data.deco->renderPass(g_pHyprOpenGL->m_renderData.pMonitor.lock(), m_data.a);
}

std::optional<CBox> CLiquidGlassPassElement::boundingBox() {
    if (!m_data.deco)
        return std::nullopt;

    auto window = m_data.deco->getOwner();
    if (!window)
        return std::nullopt;

    auto box = window->getWindowMainSurfaceBox();
    const auto pMonitor = g_pHyprOpenGL->m_renderData.pMonitor.lock();
    if (pMonitor)
        box.translate(-pMonitor->m_position);
    return box;
}

bool CLiquidGlassPassElement::needsLiveBlur() {
    return true;
}

bool CLiquidGlassPassElement::needsPrecomputeBlur() {
    return false;
}

bool CLiquidGlassPassElement::disableSimplification() {
    return true;
}
