# Liquid Glass Plugin for Hyprland

An Apple-style Liquid Glass effect plugin for [Hyprland](https://hyprland.org/). Applies a frosted glass look with full-surface lens distortion, bezel refraction with spectral dispersion, fresnel edge glow, specular highlights, environment reflection, inner shadow, and outer glow to all windows.

## Requirements

- Hyprland (matching API version)
- Hyprland shadows must be enabled for the glass effect to work correctly. The plugin **auto-enables shadows** at load time if they are disabled. Shadow visual values (range, color, etc.) can be set to 0 — only the decoration's presence in the render pipeline is required.

## Building

```bash
make
```

The plugin compiles to `hyprnux-liquid.so`.

## Installation

```bash
hyprctl plugin load /path/to/hyprnux-liquid.so
```

Or add to your Hyprland config:

```ini
plugin = /path/to/hyprnux-liquid.so
```

For [hyprpm](https://wiki.hyprland.org/Plugins/Using-Plugins/#hyprpm) users, install via the `hyprpm.toml` manifest.

## Configuration

All options live under the `plugin:liquid-glass:` namespace in your Hyprland config.

| Option | Type | Default | Description |
|---|---|---|---|
| `enabled` | int | `1` | Enable/disable the effect (0 or 1) |
| `blur_strength` | float | `1.3` | Blur radius scale. Controls the frosted glass intensity. Applied as `value * 12.0` px radius. |
| `blur_iterations` | int | `3` | Number of Gaussian blur passes (1–5). Lower values improve performance at the cost of blur quality. |
| `lens_distortion` | float | `0.5` | Full-surface convex lens distortion (0.0–1.0). Subtle barrel distortion across the entire window body. |
| `refraction_strength` | float | `0.8` | Bezel refraction displacement (0.0–1.0). How much the edge strip bends the background. |
| `chromatic_aberration` | float | `0.7` | Spectral dispersion in the bezel zone (0.0–1.0). Creates rainbow shimmer at edges via 5-tap wavelength sampling. |
| `fresnel_strength` | float | `0.6` | Edge glow intensity (0.0–1.0). Schlick-based fresnel simulating light hitting the glass surface. |
| `specular_strength` | float | `0.8` | Specular highlight brightness (0.0–1.0). Adds depth cues from two virtual light sources, extends into interior. |
| `glass_opacity` | float | `1.0` | Overall glass opacity (0.0–1.0). |
| `edge_thickness` | float | `0.045` | Bezel width as a fraction of the window's smallest dimension (0.0–0.1). |
| `tint_color` | color | `0x8899aa22` | Glass tint color in RRGGBBAA hex or `rgba(...)`. The alpha channel controls tint strength (0 = off). |
| `background_brightness` | float | `1.08` | Frosted glass brightness boost (0.5–2.0). Values > 1.0 brighten the blurred background. |
| `background_saturation` | float | `0.82` | Frosted glass desaturation (0.0–1.0). Values < 1.0 desaturate for a milky frosted look. |
| `environment_strength` | float | `0.12` | Meniscus dispersion intensity (0.0–1.0). Controls how strongly nearby/behind colors disperse along the edge in light direction. |
| `shadow_strength` | float | `0.15` | Outer glow / drop shadow intensity (0.0–1.0). Soft shadow beneath the glass element. |
| `light_angle` | float | `225.0` | Light direction in degrees (0–360). Controls which way colors disperse along the meniscus edge. 225 = top-left. |

### Example

```ini
plugin:liquid-glass {
    enabled = 1
    blur_strength = 1.3
    blur_iterations = 3
    lens_distortion = 0.5
    refraction_strength = 0.8
    chromatic_aberration = 0.7
    fresnel_strength = 0.6
    specular_strength = 0.8
    glass_opacity = 1.0
    edge_thickness = 0.045
    tint_color = rgba(88, 99, aa, 0.15)
    background_brightness = 1.08
    background_saturation = 0.82
    environment_strength = 0.12
    shadow_strength = 0.15
    light_angle = 225.0
}
```

## How It Works

The effect is composed of nine layers rendered per window:

1. **Gaussian blur** — The background behind the window is sampled with padding and blurred via a two-pass (horizontal + vertical) Gaussian shader.
2. **Full-surface lens distortion** — A convex height-map derived from the window SDF creates subtle barrel distortion across the entire window body.
3. **Bezel refraction** — An SDF-based edge strip refracts the blurred background with a smooth convex arc profile.
4. **Spectral dispersion** — 5-tap wavelength sampling in the bezel zone creates rainbow shimmer (replaces simple R/B chromatic aberration).
5. **Frosted tint** — Brightness boost and desaturation applied to the blurred background for a milky frosted look.
6. **Color tint overlay** — Configurable color tint blended onto the glass.
7. **Environment reflection** — A top-to-bottom gradient simulating ambient sky reflection on the convex glass surface.
8. **Fresnel edge glow** — Schlick-based fresnel approximation using the surface height-map.
9. **Specular highlights + inner shadow** — Two virtual light sources with wide lobes add depth, with a soft shadow at the bottom-right extending into the interior.

The plugin integrates with Hyprland's render pass system as a `DECORATION_LAYER_BOTTOM` decoration, drawing before the window surface so the glass shows through transparent windows.

## Unloading

```bash
hyprctl plugin unload /path/to/hyprnux-liquid.so
```

## License

See repository for license details.
