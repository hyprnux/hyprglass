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
    return std::nullopt;
}

bool CLiquidGlassPassElement::needsLiveBlur() {
    return false;
}

bool CLiquidGlassPassElement::needsPrecomputeBlur() {
    return false;
}
