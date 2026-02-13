// Auto-generated shader header - Do not edit!
#pragma once

#include <unordered_map>
#include <string>

inline const std::unordered_map<std::string, const char*> SHADERS = {
    {"liquidglass.frag", R"GLSL(
#version 300 es
precision highp float;

/*
 * Apple-style Liquid Glass Fragment Shader — Thick-glass refraction model
 *
 * The window is modeled as a thick convex glass slab:
 *   - Center: flat surface → clean frosted blur, no distortion
 *   - Edges: curved surface → refraction pulls in content from beyond
 *     the window boundary, creating natural color bleeding
 *
 * Rendering layers:
 * 1. Edge refraction via smooth outward direction + exponential proximity
 * 2. Chromatic aberration (per-channel refraction scale)
 * 3. Edge raw-texture blend for vivid color pickup
 * 4. Subtle center dome lens magnification
 * 5. Frosted tint (brightness boost + desaturation)
 * 6. Configurable color tint overlay
 * 7. Fresnel edge glow
 * 8. Specular highlight (top)
 * 9. Inner shadow (bottom rim)
 */

uniform sampler2D tex;
uniform vec2 fullSize;
uniform float radius;
uniform vec2 uvPadding;

uniform float refractionStrength;
uniform float chromaticAberration;
uniform float fresnelStrength;
uniform float specularStrength;
uniform float glassOpacity;
uniform float edgeThickness;
uniform vec3 tintColor;
uniform float tintAlpha;
uniform float lensDistortion;
uniform float brightness;
uniform float contrast;
uniform float saturation;
uniform float vibrancy;
uniform float vibrancyDarkness;
uniform float adaptiveDim;
uniform float adaptiveBoost;
uniform float roundingPower;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

// ============================================================================
// TEXTURE SAMPLING (window UV -> padded texture UV)
// ============================================================================

vec2 toTexUV(vec2 wuv) {
    return wuv * (1.0 - 2.0 * uvPadding) + uvPadding;
}

vec4 sampleBlurred(vec2 wuv) {
    vec2 tuv = toTexUV(wuv);
    return texture(tex, clamp(tuv, 0.001, 0.999));
}

// ============================================================================
// SDF
// ============================================================================

float lpNorm(vec2 v, float p) {
    return pow(pow(abs(v.x), p) + pow(abs(v.y), p), 1.0 / p);
}

float getRoundedBoxSDF(vec2 uv, float r) {
    vec2 p = (uv - 0.5) * fullSize;
    vec2 halfSize = fullSize * 0.5;
    float clampedR = min(r, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + clampedR;
    return min(max(q.x, q.y), 0.0) + lpNorm(max(q, 0.0), roundingPower) - clampedR;
}

float getCornerSDF(vec2 uv) {
    return getRoundedBoxSDF(uv, radius);
}

// ============================================================================
// REFRACTION DIRECTION
// Pixel-space direction toward window center — perfectly smooth everywhere,
// no SDF gradient needed. On straight edges the perpendicular pixel distance
// dominates, giving approximately edge-normal direction. At corners it
// naturally follows the diagonal.
// ============================================================================

vec2 refractionDir(vec2 uv) {
    vec2 toCenterPx = (vec2(0.5) - uv) * fullSize;
    float len = length(toCenterPx);
    return len > 0.1 ? toCenterPx / len : vec2(0.0);
}

// ============================================================================
// MAIN — Thick-glass refraction model
// ============================================================================

void main() {
    vec2 uv = v_texcoord;
    float cornerSdf = getCornerSDF(uv);

    if (cornerSdf > 0.0) {
        discard;
    }

    float cornerAlpha = 1.0 - smoothstep(-1.5, 0.5, cornerSdf);
    if (cornerAlpha < 0.001) discard;

    float minDim = min(fullSize.x, fullSize.y);
    float bezelWidthPx = edgeThickness * minDim;

    // ========================================
    // EDGE PROXIMITY + DIRECTION
    // edgeProximity: 1.0 at boundary, exponential decay inward
    // inwardDir: pixel-space direction toward center (smooth everywhere)
    // ========================================
    float edgeProximity = exp(cornerSdf / bezelWidthPx);
    vec2 inwardDir = refractionDir(uv);

    // ========================================
    // EDGE REFRACTION
    // Offset sampling UV inward (toward center) at edges — like looking
    // through the curved thick edge of a glass slab. This compresses
    // and distorts what's already behind the window, without reaching
    // beyond the window boundary.
    // ========================================
    float refractionPx = refractionStrength * 50.0;
    float refractionMag = edgeProximity * refractionPx;
    vec2 baseOffset = inwardDir * refractionMag / fullSize;

    // ========================================
    // CHROMATIC ABERRATION — per-channel refraction scale
    // Blue refracts more than red → natural spectral fringing at edges.
    // ========================================
    float chromaSpread = chromaticAberration * 0.35;
    vec2 offsetR = baseOffset * (1.0 - chromaSpread);
    vec2 offsetG = baseOffset;
    vec2 offsetB = baseOffset * (1.0 + chromaSpread);

    // ========================================
    // CENTER DOME LENS (subtle magnification in the flat interior)
    // Fades near edges so it doesn't interfere with edge refraction.
    // ========================================
    vec2 domeUV = vec2(0.0);
    if (lensDistortion > 0.001) {
        vec2 c = (uv - 0.5) * 2.0;
        vec2 dGrad = vec2(
            -4.0 * c.x * (1.0 - c.y * c.y),
            -4.0 * c.y * (1.0 - c.x * c.x)
        );
        float lensMaxPx = lensDistortion * minDim * 0.006;
        float lensFade = 1.0 - edgeProximity;
        domeUV = dGrad * lensMaxPx * lensFade / fullSize;
    }

    // ========================================
    // BACKGROUND SAMPLING (frosted blur only)
    // Nearby color influence comes naturally from the Gaussian blur
    // kernel crossing the window boundary — no explicit raw sampling.
    // ========================================
    vec3 color;
    vec2 uvR = uv + offsetR + domeUV;
    vec2 uvG = uv + offsetG + domeUV;
    vec2 uvB = uv + offsetB + domeUV;

    if (chromaticAberration > 0.001 && edgeProximity > 0.01) {
        color.r = sampleBlurred(uvR).r;
        color.g = sampleBlurred(uvG).g;
        color.b = sampleBlurred(uvB).b;
    } else {
        color = sampleBlurred(uvG).rgb;
    }

    // ========================================
    // FROSTED TINT (per-theme tone mapping)
    // ========================================
    float blurredLum = dot(color, vec3(0.2126, 0.7152, 0.0722));

    // Frosted desaturation
    color = mix(vec3(blurredLum), color, saturation);

    // Tight smoothstep range maps the blur-compressed luminance (~0.3-0.7)
    // to the full [0,1] adaptive range, creating visible per-region differentiation
    float lumCurve = smoothstep(0.25, 0.55, blurredLum);

    // Dim: multiplicative — effective at darkening bright areas
    color *= brightness * (1.0 - adaptiveDim * lumCurve);

    // Boost: additive lift — multiplicative can't brighten near-black content
    color += vec3(adaptiveBoost * (1.0 - lumCurve) * 0.5);

    // Contrast (pivot around midpoint)
    color = mix(vec3(0.5), color, contrast);

    // Vibrancy (selective saturation boost scaled by existing saturation)
    float currentLum = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float sat = max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));
    float darkFactor = 1.0 - vibrancyDarkness * (1.0 - blurredLum);
    color = mix(vec3(currentLum), color, 1.0 + vibrancy * sat * darkFactor);

    // ========================================
    // COLOR TINT OVERLAY
    // ========================================
    color = mix(color, tintColor, tintAlpha);

    // ========================================
    // FRESNEL RIM GLOW (edge zone)
    // ========================================
    if (fresnelStrength > 0.001) {
        float fresnel = edgeProximity * edgeProximity * fresnelStrength * 0.15;
        color += vec3(1.0) * fresnel;
    }

    // ========================================
    // SPECULAR — subtle top highlight (edge zone)
    // ========================================
    if (specularStrength > 0.001) {
        float topBias = pow(max(1.0 - uv.y, 0.0), 2.0);
        float spec = topBias * edgeProximity * edgeProximity * specularStrength * 0.08;
        color += vec3(1.0, 0.99, 0.97) * spec;
    }

    // ========================================
    // INNER SHADOW (bottom rim)
    // ========================================
    {
        float bottomBias = pow(uv.y, 2.0);
        float shadow = bottomBias * edgeProximity * edgeProximity * 0.06;
        color *= 1.0 - shadow;
    }

    fragColor = vec4(color, glassOpacity * cornerAlpha);
}
)GLSL"},

    {"gaussianblur.frag", R"GLSL(
#version 300 es
precision highp float;

uniform sampler2D tex;
uniform vec2 direction; // (1.0/width, 0.0) for horizontal, (0.0, 1.0/height) for vertical
uniform float blurRadius; // kernel radius in pixels

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

void main() {
    // Compute sigma from radius (covers ~3 sigma)
    float sigma = max(blurRadius / 3.0, 0.001);
    float invSigma2 = -0.5 / (sigma * sigma);

    int samples = min(int(ceil(blurRadius)), 8);

    // Center tap
    float w0 = 1.0;
    vec4 result = texture(tex, v_texcoord) * w0;
    float totalWeight = w0;

    // Linear sampling: pair adjacent taps (i, i+1) into a single bilinear fetch.
    // The interpolated offset between two texels yields their weighted average
    // in one texture() call, halving the total tap count.
    for (int i = 1; i <= samples; i += 2) {
        float x1 = float(i);
        float x2 = float(i + 1);
        float w1 = exp(x1 * x1 * invSigma2);
        float w2 = (i + 1 <= samples) ? exp(x2 * x2 * invSigma2) : 0.0;
        float wSum = w1 + w2;
        if (wSum < 0.0001) continue;

        // Offset biased toward the heavier weight
        float offset = (x1 * w1 + x2 * w2) / wSum;

        result += texture(tex, v_texcoord + direction * offset) * wSum;
        result += texture(tex, v_texcoord - direction * offset) * wSum;
        totalWeight += 2.0 * wSum;
    }

    fragColor = result / totalWeight;
}
)GLSL"},
};
