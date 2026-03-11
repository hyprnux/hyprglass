#include "GlassLayerCompositeElement.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "LayerGeometry.hpp"

#include <hyprland/src/render/OpenGL.hpp>

CGlassLayerCompositeElement::CGlassLayerCompositeElement(const SGlassLayerCompositeData& data)
    : m_data(data) {}

void CGlassLayerCompositeElement::draw(const CRegion& damage) {
    if (!m_data.layerState || !m_data.layerState->getLayerSurface())
        return;

    m_data.layerState->compositeAndRestore(g_pHyprOpenGL->m_renderData.pMonitor.lock(), m_data.alpha);
}

std::optional<CBox> CGlassLayerCompositeElement::boundingBox() {
    if (!m_data.layerState)
        return std::nullopt;

    auto layerSurface = m_data.layerState->getLayerSurface();
    if (!layerSurface)
        return std::nullopt;

    const auto monitor = g_pHyprOpenGL->m_renderData.pMonitor.lock();
    auto box = LayerGeometry::computeLayerBox(layerSurface, monitor);
    if (!box)
        return std::nullopt;

    const float padding = GlassRenderer::SAMPLE_PADDING_PX / monitor->m_scale;
    box->expand(padding);
    return box;
}

bool CGlassLayerCompositeElement::needsLiveBlur() {
    return false;
}

bool CGlassLayerCompositeElement::needsPrecomputeBlur() {
    return false;
}
