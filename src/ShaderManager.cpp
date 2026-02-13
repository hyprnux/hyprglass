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

bool CShaderManager::compileGlassShader() {
    GLuint program = g_pHyprOpenGL->createProgram(
        g_pHyprOpenGL->m_shaders->TEXVERTSRC,
        loadShaderSource("liquidglass.frag"),
        true
    );

    if (program == 0) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile glass shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    glassShader.program = program;

    glassShader.uniformLocations[SHADER_PROJ]       = glGetUniformLocation(program, "proj");
    glassShader.uniformLocations[SHADER_POS_ATTRIB] = glGetAttribLocation(program, "pos");
    glassShader.uniformLocations[SHADER_TEX_ATTRIB] = glGetAttribLocation(program, "texcoord");
    glassShader.uniformLocations[SHADER_TEX]        = glGetUniformLocation(program, "tex");
    glassShader.uniformLocations[SHADER_FULL_SIZE]  = glGetUniformLocation(program, "fullSize");
    glassShader.uniformLocations[SHADER_RADIUS]     = glGetUniformLocation(program, "radius");

    glassUniforms.refractionStrength  = glGetUniformLocation(program, "refractionStrength");
    glassUniforms.chromaticAberration = glGetUniformLocation(program, "chromaticAberration");
    glassUniforms.fresnelStrength     = glGetUniformLocation(program, "fresnelStrength");
    glassUniforms.specularStrength    = glGetUniformLocation(program, "specularStrength");
    glassUniforms.glassOpacity        = glGetUniformLocation(program, "glassOpacity");
    glassUniforms.edgeThickness       = glGetUniformLocation(program, "edgeThickness");
    glassUniforms.uvPadding           = glGetUniformLocation(program, "uvPadding");
    glassUniforms.tintColor           = glGetUniformLocation(program, "tintColor");
    glassUniforms.tintAlpha           = glGetUniformLocation(program, "tintAlpha");
    glassUniforms.lensDistortion      = glGetUniformLocation(program, "lensDistortion");
    glassUniforms.brightness          = glGetUniformLocation(program, "brightness");
    glassUniforms.contrast            = glGetUniformLocation(program, "contrast");
    glassUniforms.saturation          = glGetUniformLocation(program, "saturation");
    glassUniforms.vibrancy            = glGetUniformLocation(program, "vibrancy");
    glassUniforms.vibrancyDarkness    = glGetUniformLocation(program, "vibrancyDarkness");
    glassUniforms.adaptiveDim         = glGetUniformLocation(program, "adaptiveDim");
    glassUniforms.adaptiveBoost       = glGetUniformLocation(program, "adaptiveBoost");
    glassUniforms.roundingPower       = glGetUniformLocation(program, "roundingPower");

    glassShader.createVao();
    return true;
}

bool CShaderManager::compileBlurShader() {
    GLuint program = g_pHyprOpenGL->createProgram(
        g_pHyprOpenGL->m_shaders->TEXVERTSRC,
        loadShaderSource("gaussianblur.frag"),
        true
    );

    if (program == 0) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile blur shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    blurShader.program = program;

    blurShader.uniformLocations[SHADER_PROJ]       = glGetUniformLocation(program, "proj");
    blurShader.uniformLocations[SHADER_POS_ATTRIB] = glGetAttribLocation(program, "pos");
    blurShader.uniformLocations[SHADER_TEX_ATTRIB] = glGetAttribLocation(program, "texcoord");
    blurShader.uniformLocations[SHADER_TEX]        = glGetUniformLocation(program, "tex");

    blurUniforms.direction = glGetUniformLocation(program, "direction");
    blurUniforms.radius    = glGetUniformLocation(program, "blurRadius");

    blurShader.createVao();
    return true;
}

void CShaderManager::initializeIfNeeded() {
    if (m_initialized)
        return;

    if (!compileGlassShader())
        return;

    if (!compileBlurShader())
        return;

    m_initialized = true;
}

void CShaderManager::destroy() noexcept {
    glassShader.destroy();
    blurShader.destroy();
    m_initialized = false;
}
