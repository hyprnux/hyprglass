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
    if (!glassShader->createProgram(
            g_pHyprOpenGL->m_shaders->TEXVERTSRC,
            loadShaderSource("liquidglass.frag"),
            true
        )) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile glass shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    const auto program = glassShader->program();

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

    return true;
}

bool CShaderManager::compileBlurShader() {
    if (!blurShader->createProgram(
            g_pHyprOpenGL->m_shaders->TEXVERTSRC,
            loadShaderSource("gaussianblur.frag"),
            true
        )) {
        HyprlandAPI::addNotification(PHANDLE,
            std::format("[{}] Failed to compile blur shader", PLUGIN_NAME),
            CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
        return false;
    }

    const auto program = blurShader->program();

    blurUniforms.direction = glGetUniformLocation(program, "direction");
    blurUniforms.radius    = glGetUniformLocation(program, "blurRadius");

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
    glassShader->destroy();
    blurShader->destroy();
    m_initialized = false;
}
