#include "GlassSubsurfaceCompositeElement.hpp"
#include "GlassRenderer.hpp"
#include "GlassSubsurfaceState.hpp"
#include "SubsurfaceGeometry.hpp"

CGlassSubsurfaceCompositeElement::CGlassSubsurfaceCompositeElement(SData data)
    : m_data(std::move(data)) {}

std::vector<UP<IPassElement>> CGlassSubsurfaceCompositeElement::draw() {
    // CRegion::getExtents() is non-const, so compositeAndRestore() takes the
    // region by mutable reference — m_data.transformedRegion is this element's
    // own copy (see main.cpp's hkRenderPassAdd), safe to hand out.
    if (m_data.state)
        m_data.state->compositeAndRestore(m_data.monitor.lock(), m_data.rawBox, m_data.transformBox, m_data.transformedRegion, m_data.alpha);

    return {};
}

std::optional<CBox> CGlassSubsurfaceCompositeElement::boundingBox() {
    // Same helper + inputs as CGlassSubsurfacePassElement::boundingBox(), so the
    // pre- and post-surface elements never disagree about the item's box.
    return SubsurfaceGeometry::toPaddedLogicalBox(m_data.itemLogicalBox, m_data.monitor.lock(), GlassRenderer::SAMPLE_PADDING_PX);
}

bool CGlassSubsurfaceCompositeElement::needsLiveBlur() {
    // Always false — see CGlassLayerCompositeElement::needsLiveBlur() (Pass.cpp
    // asserts a bounding box for any element reporting live blur, and this
    // element's boundingBox() can be nullopt if the monitor died mid-frame).
    return false;
}

bool CGlassSubsurfaceCompositeElement::needsPrecomputeBlur() {
    return false;
}
