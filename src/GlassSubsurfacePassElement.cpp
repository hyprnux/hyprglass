#include "GlassSubsurfacePassElement.hpp"
#include "GlassRenderer.hpp"
#include "GlassSubsurfaceState.hpp"
#include "PluginConfig.hpp"
#include "SubsurfaceGeometry.hpp"

CGlassSubsurfacePassElement::CGlassSubsurfacePassElement(SData data)
    : m_data(std::move(data)) {}

std::vector<UP<IPassElement>> CGlassSubsurfacePassElement::draw() {
    if (currentDebugMode() == EDebugMode::HINTS_ONLY)
        return {};

    if (m_data.state)
        m_data.state->sampleAndRedirect(m_data.monitor.lock(), m_data.transformBox, m_data.alpha);

    return {};
}

std::optional<CBox> CGlassSubsurfacePassElement::boundingBox() {
    return SubsurfaceGeometry::toPaddedLogicalBox(m_data.itemLogicalBox, m_data.monitor.lock(), GlassRenderer::SAMPLE_PADDING_PX);
}

bool CGlassSubsurfacePassElement::needsLiveBlur() {
    if (currentDebugMode() == EDebugMode::GL_WORK_ONLY)
        return false;

    // Must agree with boundingBox() on whether a box exists — see
    // CGlassLayerPassElement::needsLiveBlur() for why (Pass.cpp asserts this).
    return boundingBox().has_value();
}

bool CGlassSubsurfacePassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassSubsurfacePassElement::disableSimplification() {
    // Left enabled — same reasoning as CGlassLayerPassElement: a discarded
    // element behind unchanged damage draws nothing, correctly, and
    // CGlassSubsurfaceState::m_redirectedThisFrame is the safety net against
    // the composite element surviving alone.
    return false;
}
