#include "GlassPassElement.hpp"
#include "GlassDecoration.hpp"
#include "Globals.hpp"
#include "WindowGeometry.hpp"
#include <hyprland/src/render/Renderer.hpp>

CGlassPassElement::CGlassPassElement(const SGlassPassData& data)
    : m_data(data) {}

std::vector<UP<IPassElement>> CGlassPassElement::draw() {
    if (!m_data.decoration)
        return {};

    m_data.decoration->renderPass(g_pHyprRenderer->m_renderData.pMonitor.lock(), m_data.alpha);
    return {};
}

std::optional<CBox> CGlassPassElement::boundingBox() {
    if (!m_data.decoration)
        return std::nullopt;

    auto window = m_data.decoration->getOwner();
    if (!window)
        return std::nullopt;

    const auto monitor = g_pHyprRenderer->m_renderData.pMonitor.lock();
    auto box = WindowGeometry::computeWindowBox(window, monitor);
    if (!box)
        return std::nullopt;

    // Expand by our sampling padding so the render pass damages the full
    // area we read from. Without this, wallpaper outside the window box
    // but inside our padding isn't re-rendered, leaving stale content.
    const float padding = GlassRenderer::SAMPLE_PADDING_PX / monitor->m_scale;
    box->expand(padding);
    return box;
}

bool CGlassPassElement::needsLiveBlur() {
    // Windows need live blur so the render pass fully re-renders the
    // background behind the glass before we sample it. Without this,
    // partial damage (e.g. typing in a window below) leaves stale pixels
    // in the padded sampling region, causing blinking artifacts.
    // Layers don't need this — they have their own blur cache with
    // scene generation tracking.
    return m_data.decoration && m_data.decoration->getOwner();
}

bool CGlassPassElement::needsPrecomputeBlur() {
    return false;
}

bool CGlassPassElement::disableSimplification() {
    return m_data.decoration && m_data.decoration->getOwner();
}
