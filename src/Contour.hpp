#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <hyprland/src/helpers/memory/Memory.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprutils/math/Box.hpp>

// Contour: a layer with mask_mode = contour gets edge, bend and highlights along the
// outline its app actually draws, instead of along the layer's rectangle. The outline is
// a signed distance field, built on the GPU from the layer's own render at a fraction of
// its size, and only rebuilt when the layer's content changes.
namespace Contour {

// edge_thickness is a fraction of this many logical px (at most the layer's shorter side),
// not of the layer: a card in a screen-sized layer gets a card's edge, not a screen's.
inline constexpr float REFERENCE_SIZE_LOGICAL = 160.0f;

// How far the field reaches from the outline, in bezel widths: the edge effects fade out
// well before it. Never less than what the bevel line needs, and bounded for cost.
inline constexpr float REACH_PER_BEZEL = 4.0f;
inline constexpr float REACH_MIN_PX    = 16.0f;
inline constexpr float REACH_MAX_PX    = 256.0f;

// The field has at most this many texels, and its distance transform looks at most this
// many texels either way (and texels times that stays within the work budget): all of it
// sets the cost of a rebuild. The resolution divisor (1, 2, 4 or 8 framebuffer px per
// texel) is the smallest that respects them; the coarsest one is taken whatever the work.
// 300'000: a screen-sized layer (a full-screen overlay with a card in it) gets its
// field at 1/4 resolution, not 1/2 -- a rebuild ~0.8 ms instead of ~8 on an iGPU, with
// the same look (within 3/255): the field is smoothed, and the cut is the mask's
inline constexpr int  MAX_TEXELS       = 300'000;
inline constexpr int  MAX_REACH_TEXELS = 64;
inline constexpr long MAX_WORK         = 24'000'000;
inline constexpr int  MAX_DIVISOR      = 8;
// No field side goes past what any GPU takes as a texture size, whatever the area.
inline constexpr int  MAX_FIELD_SIDE   = 8192;

// Squared distances live in half floats: this stands for "none within reach" (the shaders'
// FAR) and has to stay above any squared distance the passes can add up to.
inline constexpr float NO_DISTANCE_SQUARED = 16384.0f;
static_assert(2 * MAX_REACH_TEXELS * MAX_REACH_TEXELS < NO_DISTANCE_SQUARED);

// The Gaussian that rounds the creases where distances from two edges meet.
inline constexpr float SMOOTH_SIGMA_PER_BEZEL = 0.3f;
inline constexpr float SMOOTH_SIGMA_MIN_PX    = 1.0f;
inline constexpr float SMOOTH_SIGMA_MAX_PX    = 12.0f;
inline constexpr float SMOOTH_SIGMA_MIN_TEXELS = 0.75f;
inline constexpr int   SMOOTH_MAX_RADIUS      = 12; // texels each side; the shader loops to a uniform

// A client that commits every frame still rebuilds at most this often: the last change
// of a burst is rebuilt once the interval has passed.
// 16 ms: a layer animating at 60 fps keeps its edge on its outline every frame, now
// that a rebuild is cheap (at 33 ms the edge trailed the content every other frame)
inline constexpr std::chrono::milliseconds MIN_REBUILD_INTERVAL{16};

// While the layer fades in, its alpha is too coarse for a clean outline: that field is
// provisional, and rebuilt once the layer is at least this opaque.
inline constexpr float COMPLETE_ALPHA = 0.98f;

// A build waits for a frame that redraws the whole layer. After this many tries in a row it
// stops asking for more damage, so a layer that is never fully redrawn cannot loop the renderer.
inline constexpr int MAX_UNCOVERED_FRAMES = 6;

// What a field is built for. Equal plans mean the same texels mean the same thing.
struct SPlan {
    int   boxWidth = 0, boxHeight = 0;    // the layer box, framebuffer px
    int   divisor = 1;                    // framebuffer px per texel
    int   fieldWidth = 0, fieldHeight = 0;
    float referencePx = 0.0f;             // what edge_thickness is a fraction of
    float reachPx = 0.0f;                 // the stored distance is clamped to +-this
    int   reachTexels = 0;
    int   smoothRadius = 0;
    float smoothNegHalfInvSigmaSq = 0.0f;

    [[nodiscard]] bool operator==(const SPlan&) const noexcept = default;
};

// What edge_thickness is a fraction of, framebuffer px.
[[nodiscard]] inline float referencePx(int boxWidth, int boxHeight, float monitorScale) noexcept {
    const float scale = monitorScale > 0.0f ? monitorScale : 1.0f;
    return std::min(static_cast<float>(std::min(boxWidth, boxHeight)), REFERENCE_SIZE_LOGICAL * scale);
}

// bezelPx: the bezel width, rimPx: the bevel line's width, both framebuffer px. Nullopt for
// a box too big for any resolution to stay within MAX_TEXELS, or a non-finite input.
[[nodiscard]] inline std::optional<SPlan> makePlan(int boxWidth, int boxHeight, float bezelPx, float rimPx, float monitorScale) noexcept {
    if (boxWidth < 1 || boxHeight < 1 || !std::isfinite(bezelPx) || !std::isfinite(rimPx))
        return std::nullopt;

    SPlan plan;
    plan.boxWidth    = boxWidth;
    plan.boxHeight   = boxHeight;
    plan.referencePx = referencePx(boxWidth, boxHeight, monitorScale);
    plan.reachPx     = std::clamp(std::max(REACH_PER_BEZEL * std::max(bezelPx, 0.0f), 2.0f * std::max(rimPx, 0.0f)), REACH_MIN_PX, REACH_MAX_PX);

    for (int divisor = 1; divisor <= MAX_DIVISOR; divisor *= 2) {
        const long width  = (boxWidth + divisor - 1) / divisor;
        const long height = (boxHeight + divisor - 1) / divisor;
        const int  reach  = static_cast<int>(std::ceil(plan.reachPx / static_cast<float>(divisor)));
        if (width * height > MAX_TEXELS || width > MAX_FIELD_SIDE || height > MAX_FIELD_SIDE || reach > MAX_REACH_TEXELS)
            continue;
        if (width * height * reach > MAX_WORK && divisor < MAX_DIVISOR)
            continue;

        plan.divisor     = divisor;
        plan.fieldWidth  = static_cast<int>(width);
        plan.fieldHeight = static_cast<int>(height);
        plan.reachTexels = reach;

        const float sigmaPx     = std::clamp(SMOOTH_SIGMA_PER_BEZEL * std::max(bezelPx, 0.0f), SMOOTH_SIGMA_MIN_PX, SMOOTH_SIGMA_MAX_PX);
        const float sigmaTexels = std::max(sigmaPx / static_cast<float>(divisor), SMOOTH_SIGMA_MIN_TEXELS);
        plan.smoothRadius            = std::min(static_cast<int>(std::ceil(3.0f * sigmaTexels)), SMOOTH_MAX_RADIUS);
        plan.smoothNegHalfInvSigmaSq = -0.5f / (sigmaTexels * sigmaTexels);
        return plan;
    }

    return std::nullopt;
}

// What a build depends on besides the plan.
struct SBuildKey {
    SPlan plan;
    float threshold   = 0.0f; // mask_threshold, before the layer's fade
    bool  provisional = false; // built while the layer was still fading in
    // Where the box sits in the framebuffer, only while part of it is off the screen: the
    // visible part is all that was drawn. (0, 0) while it is entirely on screen.
    int   boxX = 0, boxY = 0;

    [[nodiscard]] bool operator==(const SBuildKey&) const noexcept = default;
};

// Whether the GPU can render to the half-float field. Asked once; allocating a framebuffer
// it cannot render to would abort the compositor. Needs a current GL context.
[[nodiscard]] bool gpuSupportsField();

// The framebuffers every field's build shares, one build at a time. Held while any layer
// holds a field, freed with the last one.
struct SScratch {
    SP<Render::IFramebuffer> first;
    SP<Render::IFramebuffer> second;
    int                      users = 0;
};

// One layer's field and when it is stale.
class CField {
  public:
    CField() = default;
    ~CField();
    CField(const CField&)            = delete;
    CField& operator=(const CField&) = delete;

    // True while the layer asked for a field and has none, or an out-of-date one.
    [[nodiscard]] bool pending() const noexcept { return m_requested && (!m_field || m_dirty); }
    [[nodiscard]] bool held() const noexcept { return static_cast<bool>(m_field); }

    // A build is due: no field yet, the content changed, or what it depends on did.
    [[nodiscard]] bool wantsBuild(const SBuildKey& key) const noexcept { return !m_field || m_dirty || key != m_builtKey; }

    // The field can be drawn for this geometry (not necessarily a fresh one).
    [[nodiscard]] bool usableFor(const SPlan& plan) const noexcept { return m_field && m_builtKey.plan == plan; }

    // The layer's content changed. Now: mark it dirty, the caller damages the layer.
    // Deferred: too soon after the last build, owed at deferredAt().
    enum class EInvalidation { None, Now, Deferred };
    [[nodiscard]] EInvalidation invalidate(std::chrono::steady_clock::time_point now) noexcept;
    [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> deferredAt() const noexcept { return m_deferredAt; }
    // Applies an owed invalidation once its time came: Now when it became dirty, Deferred
    // while it still is not due, None when nothing was owed.
    [[nodiscard]] EInvalidation flushDeferred(std::chrono::steady_clock::time_point now) noexcept;

    // A build wanted a frame that redraws the whole layer and this one did not. True while the
    // caller should still ask for more damage.
    [[nodiscard]] bool noteUncoveredFrame() noexcept { return ++m_uncoveredFrames <= MAX_UNCOVERED_FRAMES; }

    // Builds the field from `layerRender` (the layer's own render, its alpha is the content)
    // for `key`. `box` is the layer box in that framebuffer's pixels, `fadeAlpha` the layer's
    // current alpha. `callerFramebuffer` is re-bound afterwards. False when the GPU or the
    // shaders cannot do it (the caller falls back to the layer's rectangle), or on a failed allocation.
    [[nodiscard]] bool build(const SBuildKey& key, const SP<Render::IFramebuffer>& layerRender, const CBox& box, float fadeAlpha,
                             const SP<Render::IFramebuffer>& callerFramebuffer);

    // Frees the field, and the shared scratch with the last one.
    void release() noexcept;

    // The texture the glass shader reads.
    [[nodiscard]] SP<Render::IFramebuffer> framebuffer() const { return m_field; }

    // Marks the layer as using contour, so that pending() starts to report.
    void request() noexcept { m_requested = true; }

  private:
    SP<Render::IFramebuffer>                                  m_field;
    SBuildKey                                                 m_builtKey;
    bool                                                      m_requested = false;
    bool                                                      m_dirty     = false;
    bool                                                      m_counted   = false; // counted in SScratch::users
    int                                                       m_uncoveredFrames = 0;
    std::chrono::steady_clock::time_point                     m_lastBuild{};
    std::optional<std::chrono::steady_clock::time_point>      m_deferredAt;
};

} // namespace Contour
