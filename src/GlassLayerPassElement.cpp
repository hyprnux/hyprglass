#include "GlassLayerPassElement.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "LayerGeometry.hpp"

#include <hyprland/src/render/OpenGL.hpp>

CGlassLayerPassElement::CGlassLayerPassElement(const SGlassLayerPassData& data)
    : m_data(data) {}

void CGlassLayerPassElement::draw(const CRegion& damage) {
    if (!m_data.layerState || !m_data.layerState->getLayerSurface())
        return;

    m_data.layerState->sampleAndRedirect(g_pHyprOpenGL->m_renderData.pMonitor.lock(), m_data.alpha);
}

std::optional<CBox> CGlassLayerPassElement::boundingBox() {
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

bool CGlassLayerPassElement::needsLiveBlur() {
    return m_data.layerState && m_data.layerState->getLayerSurface();
}

bool CGlassLayerPassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassLayerPassElement::disableSimplification() {
    return m_data.layerState && m_data.layerState->getLayerSurface();
}
