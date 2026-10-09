#include "GlassSubsurfaceCompositeElement.hpp"
#include "GlassRenderer.hpp"
#include "GlassSubsurfaceState.hpp"
#include "SubsurfaceGeometry.hpp"
#include "RenderGuards.hpp"

CGlassSubsurfaceCompositeElement::CGlassSubsurfaceCompositeElement(SData data)
    : m_data(std::move(data)) {}

std::vector<UP<IPassElement>> CGlassSubsurfaceCompositeElement::draw(Render::CRenderContext& ctx) {
    // No foreign-render guard: the pre-surface redirect itself makes currentFB differ
    // from mainFB. compositeAndRestore() restores first, then bails if nothing was redirected.

    // CRegion::getExtents() is non-const, so compositeAndRestore() takes the
    // region by mutable reference — m_data.transformedRegion is this element's
    // own copy (see main.cpp's hkRenderPassAdd), safe to hand out.
    if (m_data.state)
        m_data.state->compositeAndRestore(ctx, m_data.monitor.lock(), m_data.rawBox, m_data.transformBox, m_data.transformedRegion, m_data.alpha);

    return {};
}

std::optional<CBox> CGlassSubsurfaceCompositeElement::boundingBox(Render::CRenderContext& ctx) {
    if (RenderGuards::shouldSkipGlass(ctx))
        return std::nullopt;

    // Same helper + inputs as CGlassSubsurfacePassElement::boundingBox(), so the
    // pre- and post-surface elements never disagree about the item's box.
    return SubsurfaceGeometry::toPaddedLogicalBox(m_data.itemLogicalBox, m_data.monitor.lock(), GlassRenderer::SAMPLE_PADDING_PX);
}

bool CGlassSubsurfaceCompositeElement::needsLiveBlur(Render::CRenderContext&) {
    // Always false — see CGlassLayerCompositeElement::needsLiveBlur() (Pass.cpp
    // asserts a bounding box for any element reporting live blur, and this
    // element's boundingBox() can be nullopt if the monitor died mid-frame).
    return false;
}

bool CGlassSubsurfaceCompositeElement::needsPrecomputeBlur(Render::CRenderContext&) {
    return false;
}
