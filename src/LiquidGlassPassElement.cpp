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

    const auto pMonitor = g_pHyprOpenGL->m_renderData.pMonitor.lock();
    if (!pMonitor)
        return std::nullopt;

    const auto workspace = window->m_workspace;
    const auto workspaceOffset = workspace && !window->m_pinned
        ? workspace->m_renderOffset->value()
        : Vector2D();

    auto box = window->getWindowMainSurfaceBox();
    box.translate(workspaceOffset);
    box.translate(-pMonitor->m_position + window->m_floatingOffset);
    box.scale(pMonitor->m_scale);
    box.round();
    return box;
}

bool CLiquidGlassPassElement::needsLiveBlur() {
    return true;
}

bool CLiquidGlassPassElement::needsPrecomputeBlur() {
    return false;
}

bool CLiquidGlassPassElement::disableSimplification() {
    // The glass effect samples and blurs the full window background area.
    // Simplification would reduce our damage when opaque windows overlap,
    // causing partial blur, stale content, and visible seam artifacts.
    return true;
}
