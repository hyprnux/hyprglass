#pragma once

#include "WindowGeometry.hpp"

#include <array>
#include <cmath>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/protocols/core/Compositor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Region.hpp>
#include <optional>

// Geometry helpers for "subsurface item glass": a glass item is a wl_subsurface
// of an app window (e.g. a toolbar capsule), enqueued into the render pass as a
// CSurfacePassElement. CSurfacePassElement::getTexBox() already returns the
// item's box in monitor-local LOGICAL coordinates — the exact contract
// IPassElement::boundingBox() expects (see SurfacePassElement.cpp) — so unlike
// CGlassLayerSurface (which recomputes its box from a long-lived PHLLS on every
// call), all geometry here is derived once from a box handed in by the caller,
// captured at CRenderPass::add() hook time while the CSurfacePassElement is
// still in hand. WindowGeometry::applyMonitorTransform() is reused verbatim for
// the 90/270-degree monitor rotation case, the same helper CGlassPassElement
// and CGlassDecoration already share.
namespace SubsurfaceGeometry {

// itemLogicalBox: CSurfacePassElement::getTexBox() for the item's subsurface,
// unmodified. Converts to a monitor-local PIXEL box (pre-rotation-transform) —
// the "rawBox" family GlassRenderer::sampleBackground()/applyGlassEffect() use
// for the desktop, matching CGlassLayerSurface's computeLayerBox() output.
[[nodiscard]] inline std::optional<CBox> toPixelBox(CBox itemLogicalBox, PHLMONITOR monitor) {
    if (!monitor)
        return std::nullopt;

    itemLogicalBox.scale(monitor->m_scale).round().noNegativeSize();

    if (!std::isfinite(itemLogicalBox.x) || !std::isfinite(itemLogicalBox.y) ||
        !std::isfinite(itemLogicalBox.w) || !std::isfinite(itemLogicalBox.h) ||
        itemLogicalBox.w <= 0.0 || itemLogicalBox.h <= 0.0)
        return std::nullopt;

    return itemLogicalBox;
}

// rawBox: toPixelBox() result. Applies the monitor rotation transform to land in
// the same post-transform pixel space as the source/target framebuffers — the
// "transformBox" family, matching CGlassLayerSurface's transformedLayerBox().
[[nodiscard]] inline CBox toTransformBox(CBox rawBox, PHLMONITOR monitor) {
    auto box = WindowGeometry::applyMonitorTransform(rawBox, monitor);
    box.noNegativeSize().round();
    return box;
}

// itemLogicalBox: CSurfacePassElement::getTexBox(), unmodified. Padded for the
// IPassElement::boundingBox() contract (monitor-local logical, pre-scale) so the
// render pass's damage expansion (needsLiveBlur) re-renders the desktop under
// the item before we sample it — mirrors LayerGeometry::computePaddedLogicalLayerBox().
[[nodiscard]] inline std::optional<CBox> toPaddedLogicalBox(CBox itemLogicalBox, PHLMONITOR monitor, float paddingPx) {
    if (!monitor)
        return std::nullopt;

    const float scale = monitor->m_scale > 0.0f ? monitor->m_scale : 1.0f;
    itemLogicalBox.expand(paddingPx / scale).noNegativeSize().round();

    if (!std::isfinite(itemLogicalBox.x) || !std::isfinite(itemLogicalBox.y) ||
        !std::isfinite(itemLogicalBox.w) || !std::isfinite(itemLogicalBox.h) ||
        itemLogicalBox.w <= 0.0 || itemLogicalBox.h <= 0.0)
        return std::nullopt;

    return itemLogicalBox;
}

// ext-background-effect-v1's blur region is surface-local logical coordinates,
// clipped to the subsurface's own committed size. Maps it into rawBox's pixel
// space (same scale/translate/transform sequence as
// CGlassLayerSurface::transformedBlurRegion()) so it lands in the same
// post-transform space as transformBox, ready for the shader's regionRects mask.
[[nodiscard]] inline CRegion transformedItemBlurRegion(const SP<CWLSurfaceResource>& surface, const CBox& rawBox, PHLMONITOR monitor) {
    if (!surface || !monitor)
        return {};

    const auto wlSurface = Desktop::View::CWLSurface::fromResource(surface);
    if (!wlSurface)
        return {};

    const auto surfaceSize = surface->m_current.size;

    CRegion region = wlSurface->m_blurRegion.copy();
    region.intersect(0, 0, surfaceSize.x, surfaceSize.y);
    region.scale(static_cast<float>(monitor->m_scale));
    region.translate(rawBox.pos());
    region.intersect(rawBox.x, rawBox.y, rawBox.w, rawBox.h);
    region.transform(Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform)),
                      monitor->m_transformedSize.x, monitor->m_transformedSize.y);
    return region;
}

// Permutes the four corner radii (top-left, top-right, bottom-right,
// bottom-left) to follow the same rotation/mirror WindowGeometry::
// applyMonitorTransform() applies to a box: a rigid transform maps a
// rectangle's corners onto one another independent of its size or position,
// so this permutation depends only on the monitor's transform. Uses the
// identical transform value applyMonitorTransform() derives, so a radii
// array and a box transformed through the two never disagree about which
// corner is which. A no-op whenever all four radii are equal.
[[nodiscard]] inline std::array<float, 4> permuteRadiiForMonitorTransform(const std::array<float, 4>& radii, PHLMONITOR monitor) {
    if (!monitor)
        return radii;

    switch (Math::wlTransformToHyprutils(Math::invertTransform(monitor->m_transform))) {
        case HYPRUTILS_TRANSFORM_NORMAL: return radii;
        case HYPRUTILS_TRANSFORM_90: return {radii[3], radii[0], radii[1], radii[2]};
        case HYPRUTILS_TRANSFORM_180: return {radii[2], radii[3], radii[0], radii[1]};
        case HYPRUTILS_TRANSFORM_270: return {radii[1], radii[2], radii[3], radii[0]};
        case HYPRUTILS_TRANSFORM_FLIPPED: return {radii[1], radii[0], radii[3], radii[2]};
        case HYPRUTILS_TRANSFORM_FLIPPED_90: return {radii[0], radii[3], radii[2], radii[1]};
        case HYPRUTILS_TRANSFORM_FLIPPED_180: return {radii[3], radii[2], radii[1], radii[0]};
        case HYPRUTILS_TRANSFORM_FLIPPED_270: return {radii[2], radii[1], radii[0], radii[3]};
    }
    return radii;
}

} // namespace SubsurfaceGeometry
