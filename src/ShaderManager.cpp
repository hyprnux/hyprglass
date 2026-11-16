#include "ShaderManager.hpp"
#include "Globals.hpp"
#include "Shaders.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/OpenGL.hpp>

std::string CShaderManager::loadShaderSource(const char* fileName) {
    if (SHADERS.contains(fileName))
        return SHADERS.at(fileName);

    const std::string message = std::format("[{}] Failed to load shader: {}", PLUGIN_NAME, fileName);
    HyprlandAPI::addNotification(PHANDLE, message, CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
    throw std::runtime_error(message);
}

bool CShaderManager::compileGlassProgram(SGlassProgram& glass, GlassFeatureMask features) {
    if (!glass.shader->createProgram(
            g_pHyprOpenGL->m_shaders->TEXVERTSRC,
            glassVariantSource(loadShaderSource("liquidglass.frag"), features),
            true,
            // A variant failure falls back to the default program; our notification is enough.
            features != GLASS_FEATURE_NONE
        ))
        return false;

    const auto program       = glass.shader->program();
    auto&      glassUniforms = glass.uniforms;

    glassUniforms.refractionStrength  = glGetUniformLocation(program, "refractionStrength");
    glassUniforms.noiseStrength       = glGetUniformLocation(program, "noiseStrength");
    glassUniforms.chromaticAberration = glGetUniformLocation(program, "chromaticAberration");
    glassUniforms.fresnelStrength     = glGetUniformLocation(program, "fresnelStrength");
    glassUniforms.specularStrength    = glGetUniformLocation(program, "specularStrength");
    glassUniforms.glassOpacity        = glGetUniformLocation(program, "glassOpacity");
    glassUniforms.edgeThickness       = glGetUniformLocation(program, "edgeThickness");
    glassUniforms.invBezelWidthPx     = glGetUniformLocation(program, "invBezelWidthPx");
    glassUniforms.texUVOffset         = glGetUniformLocation(program, "texUVOffset");
    glassUniforms.texUVScale          = glGetUniformLocation(program, "texUVScale");
    glassUniforms.tintColor           = glGetUniformLocation(program, "tintColor");
    glassUniforms.tintAlpha           = glGetUniformLocation(program, "tintAlpha");
    glassUniforms.lensDistortion      = glGetUniformLocation(program, "lensDistortion");
    glassUniforms.lensMaxPx           = glGetUniformLocation(program, "lensMaxPx");
    glassUniforms.saturation          = glGetUniformLocation(program, "saturation");
    glassUniforms.vibrancyDarkness    = glGetUniformLocation(program, "vibrancyDarkness");
    glassUniforms.adaptiveDim         = glGetUniformLocation(program, "adaptiveDim");
    glassUniforms.adaptiveBoost       = glGetUniformLocation(program, "adaptiveBoost");
    glassUniforms.refractionFlow      = glGetUniformLocation(program, "refractionFlow");
    glassUniforms.refractionSpread    = glGetUniformLocation(program, "refractionSpread");
    glassUniforms.fresnelTint         = glGetUniformLocation(program, "fresnelTint");
    glassUniforms.bevelStrength       = glGetUniformLocation(program, "bevelStrength");
    glassUniforms.bevelSize           = glGetUniformLocation(program, "bevelSize");
    glassUniforms.monitorScale        = glGetUniformLocation(program, "monitorScale");
    glassUniforms.fresnelColor        = glGetUniformLocation(program, "fresnelColor");
    glassUniforms.fresnelColorAlpha   = glGetUniformLocation(program, "fresnelColorAlpha");
    glassUniforms.bevelColor          = glGetUniformLocation(program, "bevelColor");
    glassUniforms.bevelColorAlpha     = glGetUniformLocation(program, "bevelColorAlpha");
    glassUniforms.bevelTint           = glGetUniformLocation(program, "bevelTint");
    glassUniforms.bevelAngle          = glGetUniformLocation(program, "bevelAngle");
    glassUniforms.bevelShadow         = glGetUniformLocation(program, "bevelShadow");
    glassUniforms.specularAngle       = glGetUniformLocation(program, "specularAngle");
    glassUniforms.invFullSize         = glGetUniformLocation(program, "invFullSize");
    glassUniforms.invRoundingPower    = glGetUniformLocation(program, "invRoundingPower");
    glassUniforms.radii               = glGetUniformLocation(program, "radii");
    glassUniforms.maskTex             = glGetUniformLocation(program, "maskTex");
    glassUniforms.useMask             = glGetUniformLocation(program, "useMask");
    glassUniforms.maskUVOffset        = glGetUniformLocation(program, "maskUVOffset");
    glassUniforms.maskUVScale         = glGetUniformLocation(program, "maskUVScale");
    glassUniforms.maskAlphaThreshold  = glGetUniformLocation(program, "maskAlphaThreshold");
    glassUniforms.maskCoverage        = glGetUniformLocation(program, "maskCoverage");
    glassUniforms.maskMode            = glGetUniformLocation(program, "maskMode");
    glassUniforms.regionRectCount     = glGetUniformLocation(program, "regionRectCount");
    glassUniforms.regionRects         = glGetUniformLocation(program, "regionRects[0]");
    if (glassUniforms.regionRects == -1)
        glassUniforms.regionRects = glGetUniformLocation(program, "regionRects");
    glassUniforms.sampleUVOffset      = glGetUniformLocation(program, "sampleUVOffset");
    glassUniforms.sampleUVScale       = glGetUniformLocation(program, "sampleUVScale");
    glassUniforms.glassBoxOffsetPx    = glGetUniformLocation(program, "glassBoxOffsetPx");
    glassUniforms.glassBoxSizePx      = glGetUniformLocation(program, "glassBoxSizePx");
    glassUniforms.glassUVScale        = glGetUniformLocation(program, "glassUVScale");
    glassUniforms.glassUVOffset       = glGetUniformLocation(program, "glassUVOffset");
    glassUniforms.lensHeightPx          = glGetUniformLocation(program, "lensHeightPx");
    glassUniforms.lensReflection        = glGetUniformLocation(program, "lensReflection");
    glassUniforms.lensReflectionReachPx = glGetUniformLocation(program, "lensReflectionReachPx");
    glassUniforms.focusLight            = glGetUniformLocation(program, "focusLight");
    glassUniforms.focusLightColor       = glGetUniformLocation(program, "focusLightColor");
    glassUniforms.focusLightColorAlpha  = glGetUniformLocation(program, "focusLightColorAlpha");
    glassUniforms.frostTex              = glGetUniformLocation(program, "frostTex");
    glassUniforms.frostAmount           = glGetUniformLocation(program, "frostAmount");
    glassUniforms.contourTex            = glGetUniformLocation(program, "contourTex");
    glassUniforms.contourUVScale        = glGetUniformLocation(program, "contourUVScale");
    glassUniforms.contourReferencePx    = glGetUniformLocation(program, "contourReferencePx");
    glassUniforms.contourReachPx        = glGetUniformLocation(program, "contourReachPx");

    return true;
}

bool CShaderManager::compileBlurShader() {
    if (!blurShader->createProgram(
            g_pHyprOpenGL->m_shaders->TEXVERTSRC,
            loadShaderSource("gaussianblur.frag"),
            true
        )) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile blur shader, glass is off until the plugin is reloaded", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    const auto program = blurShader->program();

    blurUniforms.direction = glGetUniformLocation(program, "direction");
    blurUniforms.radius    = glGetUniformLocation(program, "blurRadius");

    return true;
}

bool CShaderManager::compileContourPrograms() {
    const auto& vertex = g_pHyprOpenGL->m_shaders->TEXVERTSRC;

    // dynamic: a program the driver rejects comes back false instead of aborting the compositor
    if (!m_contour.seed->createProgram(vertex, loadShaderSource("contourseed.frag"), true, true) ||
        !m_contour.edt->createProgram(vertex, loadShaderSource("contouredt.frag"), true, true) ||
        !m_contour.smooth->createProgram(vertex, loadShaderSource("contoursmooth.frag"), true, true) ||
        !m_contour.pack->createProgram(vertex, loadShaderSource("contourfield.frag"), true, true))
        return false;

    auto locate = [](const SP<CShader>& shader, const char* name) { return glGetUniformLocation(shader->program(), name); };

    auto& seed         = m_contour.seedUniforms;
    seed.tex           = locate(m_contour.seed, "tex");
    seed.boxOffsetPx   = locate(m_contour.seed, "boxOffsetPx");
    seed.boxSizePx     = locate(m_contour.seed, "boxSizePx");
    seed.sourceSizePx  = locate(m_contour.seed, "sourceSizePx");
    seed.divisor       = locate(m_contour.seed, "divisor");
    seed.threshold     = locate(m_contour.seed, "threshold");

    auto& edt          = m_contour.edtUniforms;
    edt.tex            = locate(m_contour.edt, "tex");
    edt.fieldSize      = locate(m_contour.edt, "fieldSize");
    edt.direction      = locate(m_contour.edt, "direction");
    edt.reachTexels    = locate(m_contour.edt, "reachTexels");
    edt.resolveSigned  = locate(m_contour.edt, "resolveSigned");
    edt.texelPx        = locate(m_contour.edt, "texelPx");
    edt.reachPx        = locate(m_contour.edt, "reachPx");

    auto& smooth             = m_contour.smoothUniforms;
    smooth.tex               = locate(m_contour.smooth, "tex");
    smooth.fieldSize         = locate(m_contour.smooth, "fieldSize");
    smooth.direction         = locate(m_contour.smooth, "direction");
    smooth.radius            = locate(m_contour.smooth, "radius");
    smooth.negHalfInvSigmaSq = locate(m_contour.smooth, "negHalfInvSigmaSq");

    auto& pack     = m_contour.packUniforms;
    pack.tex       = locate(m_contour.pack, "tex");
    pack.fieldSize = locate(m_contour.pack, "fieldSize");
    pack.texelPx   = locate(m_contour.pack, "texelPx");

    return true;
}

const SContourPrograms* CShaderManager::contourPrograms() {
    if (m_contourCompiled)
        return &m_contour;
    if (m_contourFailed)
        return nullptr;

    if (compileContourPrograms()) {
        m_contourCompiled = true;
        return &m_contour;
    }

    m_contourFailed = true;
    m_contour.seed->destroy();
    m_contour.edt->destroy();
    m_contour.smooth->destroy();
    m_contour.pack->destroy();
    HyprlandAPI::addNotification(PHANDLE,
        std::format("[{}] The contour shaders failed to compile, layers fall back to mask_mode = alpha", PLUGIN_NAME),
        CHyprColor{1.0, 0.6, 0.2, 1.0}, 8000);
    return nullptr;
}

void CShaderManager::initializeIfNeeded() {
    if (m_initialized || m_compileFailed)
        return;

    if (!compileGlassProgram(m_defaultGlass, GLASS_FEATURE_NONE)) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile glass shader, glass is off until the plugin is reloaded", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        m_compileFailed = true;
        return;
    }

    m_initialized   = compileBlurShader();
    m_compileFailed = !m_initialized;
}

SGlassProgram& CShaderManager::glassProgram(GlassFeatureMask features) {
    features &= GLASS_FEATURE_ALL;
    if (features == GLASS_FEATURE_NONE)
        return m_defaultGlass;

    auto [variant, inserted] = m_glassVariants.try_emplace(features);
    if (inserted)
        variant->second.features = features;
    if (inserted && !compileGlassProgram(variant->second, features)) {
        variant->second.failed = true;
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] An optional glass effect failed to compile (features {:#x}), glass is drawn without it",
                        PLUGIN_NAME, features),
            CHyprColor{1.0, 0.6, 0.2, 1.0}, 8000);
    }

    return variant->second.failed ? m_defaultGlass : variant->second;
}

void CShaderManager::destroy() noexcept {
    m_defaultGlass.shader->destroy();
    m_defaultGlass.uniforms = {};
    for (auto& [features, variant] : m_glassVariants)
        variant.shader->destroy();
    m_glassVariants.clear();
    blurShader->destroy();
    if (m_contourCompiled) {
        m_contour.seed->destroy();
        m_contour.edt->destroy();
        m_contour.smooth->destroy();
        m_contour.pack->destroy();
    }
    m_contour         = {};
    m_contourCompiled = false;
    m_contourFailed   = false;
    m_initialized   = false;
    m_compileFailed = false;
}
