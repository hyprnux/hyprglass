// Auto-generated shader header - Do not edit!
#pragma once

#include <unordered_map>
#include <string>

inline const std::unordered_map<std::string, const char*> SHADERS = {
    {"liquidglass.frag", R"GLSL(
#version 300 es
precision highp float;

/*
 * Apple-style Liquid Glass Fragment Shader
 * 
 * Implements the key visual elements of Apple's iOS 26 Liquid Glass design:
 * 1. Edge refraction with displacement mapping
 * 2. Chromatic aberration (RGB channel separation)
 * 3. Fresnel effect (edge glow based on viewing angle)
 * 4. Specular highlights (sharp light reflections)
 * 5. Subtle interior blur for glass thickness
 */

// Uniforms
uniform sampler2D tex;
uniform vec2 topLeft;
uniform vec2 fullSize;
uniform vec2 fullSizeUntransformed;
uniform float radius;
uniform float time;

// Configurable parameters
uniform float blurStrength;        // Interior blur amount (0.0 - 2.0)
uniform float refractionStrength;  // Edge refraction intensity (0.0 - 0.15)
uniform float chromaticAberration; // RGB separation amount (0.0 - 0.02)
uniform float fresnelStrength;     // Edge glow intensity (0.0 - 1.0)
uniform float specularStrength;    // Highlight brightness (0.0 - 1.0)
uniform float glassOpacity;        // Overall glass opacity (0.0 - 1.0)
uniform float edgeThickness;       // How thick the refractive edge is (0.0 - 0.3)
uniform vec2 uvPadding;             // Ratio of padding on each side of the sampled texture

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

// Constants
const float PI = 3.14159265359;
const float AA_EDGE = 0.002; // Anti-aliasing edge softness

// Convert window-space UV [0,1] to padded texture UV
vec2 windowToTexUV(vec2 wuv) {
    return wuv * (1.0 - 2.0 * uvPadding) + uvPadding;
}

// Safe texture sample using window-space UV, remapped to padded texture
vec4 sampleTex(vec2 wuv) {
    vec2 tuv = windowToTexUV(wuv);
    return texture(tex, clamp(tuv, 0.001, 0.999));
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

// Compute signed distance to rounded rectangle (in UV space)
float roundedBoxSDF(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

// Get alpha mask for rounded corners
float getRoundedAlpha(vec2 uv) {
    vec2 center = vec2(0.5);
    vec2 pos = uv - center;
    
    // Convert radius from pixels to UV space, accounting for aspect ratio
    float aspectRatio = fullSize.x / fullSize.y;
    vec2 scaledPos = pos * vec2(aspectRatio, 1.0);
    
    // Half size in UV space
    vec2 halfSize = vec2(0.5 * aspectRatio, 0.5);
    
    // Radius in UV space (approximate)
    float uvRadius = radius / fullSize.y;
    
    float dist = roundedBoxSDF(scaledPos, halfSize, uvRadius);
    
    // Smooth edge for anti-aliasing
    return 1.0 - smoothstep(-AA_EDGE, AA_EDGE, dist);
}

// Smooth edge mask with configurable falloff
float getEdgeMask(vec2 uv, float thickness) {
    vec2 center = vec2(0.5);
    vec2 pos = uv - center;
    
    // Account for aspect ratio
    float aspectRatio = fullSize.x / fullSize.y;
    vec2 scaledPos = pos * vec2(aspectRatio, 1.0);
    vec2 halfSize = vec2(0.5 * aspectRatio, 0.5);
    
    // Radius in UV space
    float uvRadius = radius / fullSize.y;
    
    // Compute distance from inner edge
    float innerThickness = thickness * min(aspectRatio, 1.0);
    float dist = roundedBoxSDF(scaledPos, halfSize - innerThickness, max(uvRadius - innerThickness, 0.0));
    
    // Create smooth gradient from edge to center
    float edgeFactor = smoothstep(-thickness * 0.5, thickness * 0.5, dist);
    return clamp(edgeFactor, 0.0, 1.0);
}

// Generate refraction displacement based on edge proximity
vec2 getRefractionOffset(vec2 uv, float edgeMask) {
    vec2 center = vec2(0.5);
    vec2 fromCenter = uv - center;
    float dist = length(fromCenter);
    
    vec2 dir = normalize(fromCenter + 0.0001);
    
    // Refraction is stronger at edges (like looking through curved glass)
    float refractionAmount = edgeMask * sin(edgeMask * PI * 0.5);
    
    return dir * refractionAmount * refractionStrength;
}

// ============================================================================
// BLUR FUNCTION - Gaussian approximation
// ============================================================================

vec3 gaussianBlur(vec2 uv, vec2 texelSize, float strength) {
    vec3 result = sampleTex(uv).rgb * 0.1633;
    
    vec2 off1 = texelSize * strength;
    vec2 off2 = texelSize * strength * 2.0;
    
    result += sampleTex(uv + vec2(off1.x, 0.0)).rgb * 0.1531;
    result += sampleTex(uv - vec2(off1.x, 0.0)).rgb * 0.1531;
    result += sampleTex(uv + vec2(0.0, off1.y)).rgb * 0.1531;
    result += sampleTex(uv - vec2(0.0, off1.y)).rgb * 0.1531;
    result += sampleTex(uv + vec2(off2.x, 0.0)).rgb * 0.0561;
    result += sampleTex(uv - vec2(off2.x, 0.0)).rgb * 0.0561;
    result += sampleTex(uv + vec2(0.0, off2.y)).rgb * 0.0561;
    result += sampleTex(uv - vec2(0.0, off2.y)).rgb * 0.0561;
    
    return result;
}

// Simpler 5-tap blur for performance with bounds clamping
vec3 fastBlur(vec2 uv, vec2 texelSize, float strength) {
    vec2 off1 = texelSize * strength;
    
    vec3 result = sampleTex(uv).rgb * 0.4;
    result += sampleTex(uv + vec2(off1.x, 0.0)).rgb * 0.15;
    result += sampleTex(uv - vec2(off1.x, 0.0)).rgb * 0.15;
    result += sampleTex(uv + vec2(0.0, off1.y)).rgb * 0.15;
    result += sampleTex(uv - vec2(0.0, off1.y)).rgb * 0.15;
    
    return result;
}

// ============================================================================
// CHROMATIC ABERRATION
// ============================================================================

vec3 chromaticSample(vec2 uv, vec2 texelSize, float edgeMask) {
    float caAmount = chromaticAberration * edgeMask;
    
    vec2 center = vec2(0.5);
    vec2 dir = normalize(uv - center + 0.0001);
    
    vec2 offsetR = dir * caAmount * 0.8;
    vec2 offsetB = dir * caAmount * 1.2;
    
    float r = sampleTex(uv + offsetR).r;
    float g = sampleTex(uv).g;
    float b = sampleTex(uv + offsetB).b;
    
    return vec3(r, g, b);
}

// ============================================================================
// FRESNEL EFFECT - Edge glow based on viewing angle
// ============================================================================

float fresnelEffect(vec2 uv) {
    vec2 center = vec2(0.5);
    vec2 pos = uv - center;
    
    // Distance from center, normalized
    float dist = length(pos) * 2.0;
    
    // Fresnel approximation: stronger reflection at grazing angles
    // F = F0 + (1 - F0) * (1 - cos(theta))^5
    float fresnel = pow(dist, 3.0);
    
    // Apply edge mask to limit to actual edges
    float edgeMask = getEdgeMask(uv, edgeThickness);
    
    return fresnel * edgeMask * fresnelStrength;
}

// ============================================================================
// SPECULAR HIGHLIGHTS - Sharp light reflections
// ============================================================================

float specularHighlight(vec2 uv) {
    // Simulate light coming from top-left
    vec2 lightDir = normalize(vec2(-0.7, -0.7));
    vec2 center = vec2(0.5);
    vec2 pos = uv - center;
    
    // Dot product with light direction
    float highlight = dot(normalize(pos + 0.0001), lightDir);
    
    // Sharp falloff for specular look
    highlight = pow(max(highlight, 0.0), 16.0);
    
    // Only show on edges
    float edgeMask = getEdgeMask(uv, edgeThickness * 0.5);
    
    // Add secondary highlight from bottom-right for depth
    vec2 lightDir2 = normalize(vec2(0.7, 0.7));
    float highlight2 = dot(normalize(pos + 0.0001), lightDir2);
    highlight2 = pow(max(highlight2, 0.0), 24.0) * 0.5;
    
    return (highlight + highlight2) * edgeMask * specularStrength;
}

// ============================================================================
// MAIN SHADER
// ============================================================================

void main() {
    // v_texcoord maps [0,1] across the rendered quad (= window area)
    // We use it directly for geometry calculations (SDF, edge mask)
    // For texture sampling, windowToTexUV() remaps into the padded texture
    vec2 uv = v_texcoord;
    vec2 texelSize = 1.0 / fullSize;
    
    // Get rounded corner alpha - discard pixels outside rounded rect
    float cornerAlpha = getRoundedAlpha(uv);
    if (cornerAlpha < 0.001) {
        discard;
    }
    
    // Calculate edge mask for effects
    float edgeMask = getEdgeMask(uv, edgeThickness);
    
    // ========================================
    // 1. REFRACTION - Bend the background at edges
    // ========================================
    vec2 refractionOffset = getRefractionOffset(uv, edgeMask);
    vec2 refractedUV = uv + refractionOffset;
    
    // No need to clamp aggressively - padded texture has real pixels beyond window edge
    
    // ========================================
    // 2. BLUR - Glass thickness effect
    // ========================================
    float blurAmount = blurStrength * 0.5;
    vec3 blurredColor = fastBlur(refractedUV, texelSize, blurAmount);
    
    // ========================================
    // 3. CHROMATIC ABERRATION - Color fringing at edges
    // ========================================
    vec3 caColor = chromaticSample(refractedUV, texelSize, edgeMask);
    
    vec3 glassColor = mix(blurredColor, caColor, edgeMask * 0.5);
    
    // ========================================
    // 4. FRESNEL EFFECT - Edge glow
    // ========================================
    float fresnel = fresnelEffect(uv);
    
    // ========================================
    // 5. SPECULAR HIGHLIGHTS - Sharp reflections
    // ========================================
    float specular = specularHighlight(uv);
    
    // ========================================
    // COMBINE ALL EFFECTS
    // ========================================
    
    vec3 glassTint = vec3(0.96, 0.97, 1.0);
    vec3 finalColor = glassColor * glassTint;
    
    finalColor += vec3(1.0) * fresnel * 0.1;
    finalColor += vec3(1.0, 0.98, 0.95) * specular;
    
    float luminance = dot(finalColor, vec3(0.299, 0.587, 0.114));
    finalColor = mix(vec3(luminance), finalColor, 0.92);
    
    // Output with glass opacity and rounded corner alpha
    fragColor = vec4(finalColor, glassOpacity * cornerAlpha);
}
)GLSL"},
};
