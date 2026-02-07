#include "RectElement.hpp"
#include "globals.hpp"

#include <hyprland/src/render/OpenGL.hpp>

CRectElement::CRectElement(const CBox& box, const CHyprColor& color)
    : m_box(box), m_color(color) {}

void CRectElement::draw(const CRegion& damage) {
    if (m_box.width <= 0 || m_box.height <= 0)
        return;

    g_pHyprOpenGL->renderRect(m_box, m_color, {});
}

bool CRectElement::needsLiveBlur() {
    return false;
}

bool CRectElement::needsPrecomputeBlur() {
    return false;
}

std::optional<CBox> CRectElement::boundingBox() {
    return m_box;
}
