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
 * Rendering layers:
 * 1. Full-surface convex lens distortion (height-map from SDF)
 * 2. Bezel refraction with spectral dispersion (5-tap rainbow)
 * 3. Frosted tint (brightness boost + desaturation)
 * 4. Configurable color tint overlay
 * 5. Environment reflection (top-down gradient)
 * 6. Fresnel edge glow (Schlick approximation)
 * 7. Specular highlights (two light sources)
 * 8. Inner shadow (bottom/right depth)
 * 9. Outer glow / soft drop shadow
 */

uniform sampler2D tex;
uniform sampler2D texRaw;
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
uniform float backgroundBrightness;
uniform float backgroundSaturation;
uniform float environmentStrength;
uniform float shadowStrength;
uniform float lightAngle;
uniform float roundingPower;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

// ============================================================================
// TEXTURE SAMPLING (window UV -> padded texture UV)
// ============================================================================

vec2 toTexUV(vec2 wuv) {
    return wuv * (1.0 - 2.0 * uvPadding) + uvPadding;
}

vec4 sampleTex(vec2 wuv) {
    vec2 tuv = toTexUV(wuv);
    return texture(tex, clamp(tuv, 0.001, 0.999));
}

// Sample from the raw (unblurred) background — sharp colors for nearby pickup
vec4 sampleRaw(vec2 wuv) {
    vec2 tuv = toTexUV(wuv);
    return texture(texRaw, clamp(tuv, 0.001, 0.999));
}

// ============================================================================
// SDF
// ============================================================================

// Lp-norm: matches Hyprland's superellipse rounding (power=2 is Euclidean/circle)
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

// Bezel SDF: same shape as corner SDF so effects align with the visible boundary
float getBezelSDF(vec2 uv) {
    return getRoundedBoxSDF(uv, radius);
}

// SDF gradient (normalized) from bezel SDF
vec2 sdfNormal(vec2 uv) {
    vec2 eps = 2.0 / fullSize;
    float dx = getBezelSDF(uv + vec2(eps.x, 0.0)) - getBezelSDF(uv - vec2(eps.x, 0.0));
    float dy = getBezelSDF(uv + vec2(0.0, eps.y)) - getBezelSDF(uv - vec2(0.0, eps.y));
    vec2 n = vec2(dx, dy);
    float len = length(n);
    return len > 0.001 ? n / len : vec2(0.0);
}

// Analytical convex dome: (1-x²)(1-y²) in normalized [-1,1] UV space.
// Perfectly smooth everywhere — no SDF quadrant artifacts.
float domeHeight(vec2 uv) {
    vec2 c = (uv - 0.5) * 2.0;
    float cx2 = c.x * c.x;
    float cy2 = c.y * c.y;
    return max((1.0 - cx2) * (1.0 - cy2), 0.0);
}

// Analytical gradient of dome — smooth polynomial, no diagonal seams.
// Returns gradient in UV space (chain rule: dc/duv = 2).
vec2 domeGradient(vec2 uv) {
    vec2 c = (uv - 0.5) * 2.0;
    float cx2 = c.x * c.x;
    float cy2 = c.y * c.y;
    return vec2(
        -4.0 * c.x * (1.0 - cy2),
        -4.0 * c.y * (1.0 - cx2)
    );
}

// ============================================================================
// MAIN — 4-zone water surface tension model
//
// Cross-section (C-shape): meniscus curves outward beyond the contact line.
//   Far outside  → shadow only
//   Meniscus outer (0..+meniscusPx) → refracted light spills outside glass
//   Border (cornerSdf ≈ 0)          → contact line bright rim
//   Meniscus inner (-meniscusPx..0) → strongest dispersion zone
//   Bezel (deeper inside)           → gradual distortion fading to center
//   Flat (center)                   → frosted blur + subtle dome lens
// ============================================================================

void main() {
    vec2 uv = v_texcoord;
    float cornerSdf = getCornerSDF(uv);
    float minDim = min(fullSize.x, fullSize.y);

    // Meniscus half-width: fixed thin band for light spill, not proportional to window size
    float meniscusPx = 4.0;

    // Light direction for directional dispersion (degrees → vec2)
    float angleRad = lightAngle * 3.14159265 / 180.0;
    vec2 lightDir = vec2(cos(angleRad), sin(angleRad));

    // Dome gradient: smooth outward direction (no diagonal artifacts)
    vec2 dGrad = domeGradient(uv);

    // ========================================
    // OUTSIDE (cornerSdf > 0) — discard, Hyprland handles window shadow
    // ========================================
    if (cornerSdf > 0.0) {
        discard;
    }

    // ========================================
    // INSIDE GLASS (cornerSdf <= 0)
    // ========================================

    float cornerAlpha = 1.0 - smoothstep(-1.5, 0.5, cornerSdf);
    if (cornerAlpha < 0.001) discard;

    // Zone factors
    float meniscusFactor = exp(cornerSdf / meniscusPx);
    float bezelSdf = getBezelSDF(uv);
    float bezelWidthPx = edgeThickness * minDim;
    float bezelFactor = exp(bezelSdf / bezelWidthPx);

    vec2 normal = vec2(0.0);
    if (bezelFactor > 0.01) {
        normal = sdfNormal(uv);
    }

    // ========================================
    // FLAT ZONE: Dome lens distortion
    // ========================================
    float lensMaxPx = lensDistortion * minDim * 0.005;
    vec2 lensDisplacement = -dGrad * lensMaxPx / fullSize;

    // ========================================
    // BEZEL ZONE: Inward refraction
    // Uses dome gradient for direction instead of SDF normal to avoid
    // visible displacement rings at corners (SDF normal rotates sharply
    // around corner arcs, dome gradient transitions smoothly)
    // ========================================
    vec2 bezelDisplacement = vec2(0.0);
    if (bezelFactor > 0.01) {
        float maxPx = refractionStrength * 40.0;
        vec2 refractionDir = dGrad;
        float refrDirLen = length(refractionDir);
        if (refrDirLen > 0.01) {
            refractionDir /= refrDirLen;
        } else {
            refractionDir = vec2(0.0);
        }
        bezelDisplacement = refractionDir * bezelFactor * bezelFactor * maxPx / fullSize;
    }

    vec2 sampleUV = uv + lensDisplacement + bezelDisplacement;

    // ========================================
    // BACKGROUND SAMPLE
    // ========================================
    vec3 color = sampleTex(sampleUV).rgb;

    // ========================================
    // NEARBY COLOR PICKUP (extends across full bezel zone)
    // Detect saturated colors nearby via raw texture, apply as a tint.
    // Saturated areas (green, blue...) dominate; neutral areas (white, text) are ignored.
    // ========================================
    if (bezelFactor > 0.02) {
        vec2 outwardDir = -dGrad;
        float outwardLen = length(outwardDir);
        if (outwardLen > 0.01) {
            outwardDir /= outwardLen;
        } else {
            outwardDir = normalize(uv - 0.5);
        }
        vec2 tangent = vec2(-outwardDir.y, outwardDir.x);

        vec2 outwardUV = uv + outwardDir * 55.0 / fullSize;

        // 3-tap raw sampling to detect nearby dominant color
        vec3 rawC = sampleRaw(outwardUV).rgb * 0.5;
        rawC += sampleRaw(outwardUV + tangent * 40.0 / fullSize).rgb * 0.25;
        rawC += sampleRaw(outwardUV - tangent * 40.0 / fullSize).rgb * 0.25;

        // Compute saturation: distance from gray axis
        float rawLum = dot(rawC, vec3(0.2126, 0.7152, 0.0722));
        vec3 chroma = rawC - vec3(rawLum);
        float sat = length(chroma);

        // Only pick up saturated colors — white/gray/text gets ignored
        if (sat > 0.04) {
            // Boost the chromatic component to get a vivid tint
            vec3 tint = vec3(rawLum) + chroma * (1.0 / sat) * 0.4;
            float pickupBlend = bezelFactor * bezelFactor * min(sat * 3.0, 1.0) * 0.6;
            color = mix(color, tint, pickupBlend);
        }
    }

    // ========================================
    // MENISCUS INNER: Directional dispersion (prismatic fringing)
    // Content behind the glass gets dispersed along the edge
    // in the direction of light flow. Strongest near the contact line.
    // ========================================
    if (meniscusFactor > 0.05 && environmentStrength > 0.001) {
        vec2 tangent = vec2(-normal.y, normal.x);
        float flow = dot(tangent, lightDir);

        float dispPx = refractionStrength * environmentStrength * meniscusFactor * 200.0;
        vec2 flowOffset = tangent * flow * dispPx / fullSize;
        vec2 spreadR = tangent * dispPx * 0.4 / fullSize;

        vec3 dispersed;
        dispersed.r = sampleTex(sampleUV + flowOffset + spreadR).r;
        dispersed.g = sampleTex(sampleUV + flowOffset).g;
        dispersed.b = sampleTex(sampleUV + flowOffset - spreadR).b;

        color = mix(color, dispersed, meniscusFactor * meniscusFactor);
    }

    // ========================================
    // FROSTED TINT (brightness + desaturation)
    // ========================================
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luminance), color, backgroundSaturation);
    color *= backgroundBrightness;

    // ========================================
    // COLOR TINT OVERLAY
    // ========================================
    color = mix(color, tintColor, tintAlpha);

    // ========================================
    // FRESNEL RIM GLOW (bezel zone)
    // ========================================
    if (fresnelStrength > 0.001) {
        float fresnel = bezelFactor * bezelFactor * fresnelStrength * 0.12;
        color += vec3(1.0) * fresnel;
    }

    // ========================================
    // SPECULAR — subtle top highlight (bezel zone)
    // ========================================
    if (specularStrength > 0.001) {
        float topBias = pow(max(1.0 - uv.y, 0.0), 2.0);
        float spec = topBias * bezelFactor * bezelFactor * specularStrength * 0.08;
        color += vec3(1.0, 0.99, 0.97) * spec;
    }

    // ========================================
    // INNER SHADOW (bottom rim, bezel zone)
    // ========================================
    {
        float bottomBias = pow(uv.y, 2.0);
        float shadow = bottomBias * bezelFactor * bezelFactor * 0.06;
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

    int samples = int(ceil(blurRadius));

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
