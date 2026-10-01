#include "GlassLayerCompositeElement.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"
#include "LayerGeometry.hpp"

CGlassLayerCompositeElement::CGlassLayerCompositeElement(const SGlassLayerCompositeData& data)
    : m_data(data) {}

std::vector<UP<IPassElement>> CGlassLayerCompositeElement::draw() {
    if (m_data.layerState && m_data.layerState->getLayerSurface())
        m_data.layerState->compositeAndRestore(g_pHyprRenderer->m_renderData.pMonitor.lock(), m_data.alpha, m_data.maskSource);

    return {};
}

std::optional<CBox> CGlassLayerCompositeElement::boundingBox() {
    if (!m_data.layerState)
        return std::nullopt;

    auto layerSurface = m_data.layerState->getLayerSurface();
    if (!layerSurface)
        return std::nullopt;

    // Same helper as CGlassLayerPassElement::boundingBox(), so the pre- and
    // post-surface elements never disagree about the layer's box.
    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    return LayerGeometry::computePaddedLogicalLayerBox(layerSurface, monitor, GlassRenderer::SAMPLE_PADDING_PX);
}

bool CGlassLayerCompositeElement::needsLiveBlur() {
    // Always false: Hyprland's CRenderPass::render() asserts a bounding box
    // for any element reporting live blur ("No bounding box for an element
    // with live blur is illegal", Pass.cpp), and this element's boundingBox()
    // can be nullopt (e.g. layer torn down mid-frame).
    return false;
}

bool CGlassLayerCompositeElement::needsPrecomputeBlur() {
    return false;
}
