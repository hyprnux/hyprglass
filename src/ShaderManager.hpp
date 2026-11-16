#pragma once

#include "ShaderVariants.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/render/Shader.hpp>
#include <string>
#include <unordered_map>

struct SGlassUniforms {
    GLint refractionStrength = -1;
    GLint noiseStrength = -1;
    GLint chromaticAberration = -1;
    GLint fresnelStrength = -1;
    GLint specularStrength = -1;
    GLint glassOpacity = -1;
    GLint edgeThickness = -1;
    GLint invBezelWidthPx = -1;
    GLint texUVOffset = -1;
    GLint texUVScale = -1;
    GLint tintColor = -1;
    GLint tintAlpha = -1;
    GLint lensDistortion = -1;
    GLint lensMaxPx = -1;
    GLint saturation = -1;
    GLint vibrancyDarkness = -1;
    GLint adaptiveDim = -1;
    GLint adaptiveBoost = -1;
    GLint refractionFlow = -1;
    GLint refractionSpread = -1;
    GLint fresnelTint = -1;
    GLint bevelStrength = -1;
    GLint bevelSize = -1;
    GLint monitorScale = -1;
    GLint fresnelColor = -1;
    GLint fresnelColorAlpha = -1;
    GLint bevelColor = -1;
    GLint bevelColorAlpha = -1;
    GLint bevelTint = -1;
    GLint bevelAngle = -1;
    GLint bevelShadow = -1;
    GLint specularAngle = -1;
    GLint invFullSize = -1;
    GLint invRoundingPower = -1;
    GLint radii = -1; // per-corner radius: top-left, top-right, bottom-right, bottom-left

    // Layers only: temp FBO surface mask for content-aware glass
    GLint maskTex = -1;
    GLint useMask = -1;
    GLint maskUVOffset = -1;
    GLint maskUVScale = -1;
    GLint maskAlphaThreshold = -1;
    GLint maskCoverage = -1;
    GLint maskMode = -1;
    GLint regionRectCount = -1;
    GLint regionRects = -1;
    GLint sampleUVOffset = -1;
    GLint sampleUVScale = -1;

    // The glass shape's sub-rect of the drawn box (smaller only for subsurface
    // items), in box-local pixels, and its UV mapping (see Shaders.hpp).
    GLint glassBoxOffsetPx = -1;
    GLint glassBoxSizePx = -1;
    GLint glassUVScale = -1;
    GLint glassUVOffset = -1;

    // GLASS_FEATURE_LENS_EDGE variant only; -1 (uploads ignored) in every other program.
    GLint lensHeightPx = -1;
    GLint lensReflection = -1;
    GLint lensReflectionReachPx = -1;

    // GLASS_FEATURE_FOCUS_LIGHT variant only.
    GLint focusLight = -1;
    GLint focusLightColor = -1;
    GLint focusLightColorAlpha = -1;

    // GLASS_FEATURE_FROST variant only.
    GLint frostTex    = -1;
    GLint frostAmount = -1;

    // GLASS_FEATURE_CONTOUR variant only.
    GLint contourTex         = -1;
    GLint contourUVScale     = -1;
    GLint contourReferencePx = -1;
    GLint contourReachPx     = -1;
};

struct SBlurUniforms {
    GLint direction = -1;
    GLint radius    = -1;
};

// The programs that trace a layer's outline into a distance field (see Contour.hpp), and
// where each reads its settings. Compiled together, the first time a layer asks for a field.
struct SContourPrograms {
    SP<CShader> seed   = makeShared<CShader>();
    SP<CShader> edt    = makeShared<CShader>();
    SP<CShader> smooth = makeShared<CShader>();
    SP<CShader> pack   = makeShared<CShader>();

    struct {
        GLint tex = -1, boxOffsetPx = -1, boxSizePx = -1, sourceSizePx = -1, divisor = -1, threshold = -1;
    } seedUniforms;
    struct {
        GLint tex = -1, fieldSize = -1, direction = -1, reachTexels = -1, resolveSigned = -1, texelPx = -1, reachPx = -1;
    } edtUniforms;
    struct {
        GLint tex = -1, fieldSize = -1, direction = -1, radius = -1, negHalfInvSigmaSq = -1;
    } smoothUniforms;
    struct {
        GLint tex = -1, fieldSize = -1, texelPx = -1;
    } packUniforms;
};

// One compiled variant of the glass program, with the uniform locations of that program.
struct SGlassProgram {
    SP<CShader>    shader = makeShared<CShader>();
    SGlassUniforms uniforms;
    bool           failed = false; // variants only; m_compileFailed covers the default program
    // What this program really draws: none for the default program, a variant's own set once it compiled.
    GlassFeatureMask features = GLASS_FEATURE_NONE;
};

class CShaderManager {
  public:
    [[nodiscard]] bool isInitialized() const noexcept { return m_initialized; }
    // The default glass program or the blur program failed to compile. Not retried
    // before the plugin is reloaded: every attempt would stall the frame and notify again.
    [[nodiscard]] bool compileFailed() const noexcept { return m_compileFailed; }

    void initializeIfNeeded();
    void destroy() noexcept;

    // The glass program for a draw using `features`, compiled on its first use. A
    // variant that fails to compile is reported once, never retried, and the default
    // program draws in its place. Only call once isInitialized().
    [[nodiscard]] SGlassProgram& glassProgram(GlassFeatureMask features);

    // The contour field programs, compiled on first use. Null when they failed to compile:
    // reported once, not retried before the plugin is reloaded.
    [[nodiscard]] const SContourPrograms* contourPrograms();

    SP<CShader>    blurShader = makeShared<CShader>();
    SBlurUniforms  blurUniforms;

  private:
    bool m_initialized = false;
    bool m_compileFailed = false;

    SGlassProgram                                       m_defaultGlass;
    std::unordered_map<GlassFeatureMask, SGlassProgram> m_glassVariants;

    SContourPrograms m_contour;
    bool             m_contourCompiled = false;
    bool             m_contourFailed   = false;

    [[nodiscard]] static std::string loadShaderSource(const char* fileName);
    [[nodiscard]] static bool compileGlassProgram(SGlassProgram& program, GlassFeatureMask features);
    [[nodiscard]] bool compileBlurShader();
    [[nodiscard]] bool compileContourPrograms();
};
