#include "GlassLayerCompositeElement.hpp"
#include "GlassLayerSurface.hpp"
#include "GlassRenderer.hpp"
#include "Globals.hpp"

CGlassLayerCompositeElement::CGlassLayerCompositeElement(const SGlassLayerCompositeData& data)
    : m_data(data) {}

std::vector<UP<IPassElement>> CGlassLayerCompositeElement::draw(Render::CRenderContext& ctx) {
    if (m_data.layerState && m_data.layerState->getLayerSurface())
        m_data.layerState->compositeAndRestore(ctx, ctx.m_data.pMonitor.lock(), m_data.alpha, m_data.maskSource);

    return {};
}

std::optional<CBox> CGlassLayerCompositeElement::boundingBox(Render::CRenderContext& ctx) {
    if (!m_data.layerState)
        return std::nullopt;

    // Same helper as CGlassLayerPassElement::boundingBox(), so the pre- and
    // post-surface elements never disagree about the glass box.
    return m_data.layerState->paddedLogicalBox(ctx.m_data.pMonitor.lock());
}

bool CGlassLayerCompositeElement::needsLiveBlur(Render::CRenderContext&) {
    // Always false: Hyprland's CRenderPass::render() asserts a bounding box
    // for any element reporting live blur ("No bounding box for an element
    // with live blur is illegal", Pass.cpp), and this element's boundingBox()
    // can be nullopt (e.g. layer torn down mid-frame).
    return false;
}

bool CGlassLayerCompositeElement::needsPrecomputeBlur(Render::CRenderContext&) {
    return false;
}
