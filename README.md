# Liquid Glass Plugin for Hyprland

An Apple-style Liquid Glass effect plugin for [Hyprland](https://hyprland.org/). Applies a frosted glass look with bezel refraction, chromatic aberration, fresnel edge glow, specular highlights, and inner shadow to all windows.

## Requirements

- Hyprland (matching API version)
- Hyprland shadows **must be enabled** (`decoration:shadow:enabled = true`). Shadow values can be set to 0 — only the decoration's presence in the render pipeline is required.

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
| `blur_strength` | float | `1.0` | Blur radius scale. Controls the frosted glass intensity. Applied as `value * 12.0` px radius with 3 Gaussian iterations. |
| `refraction_strength` | float | `0.6` | Bezel refraction displacement (0.0–1.0). How much the edge strip bends the background. |
| `chromatic_aberration` | float | `0.3` | RGB channel separation in the bezel zone (0.0–1.0). |
| `fresnel_strength` | float | `0.5` | Edge glow intensity (0.0–1.0). Simulates light hitting the glass rim. |
| `specular_strength` | float | `0.5` | Specular highlight brightness (0.0–1.0). Adds depth cues from virtual light sources. |
| `glass_opacity` | float | `1.0` | Overall glass opacity (0.0–1.0). |
| `edge_thickness` | float | `0.08` | Bezel width as a fraction of the window's smallest dimension (0.0–0.1). |

### Example

```ini
plugin:liquid-glass {
    enabled = 1
    blur_strength = 1.0
    refraction_strength = 0.6
    chromatic_aberration = 0.3
    fresnel_strength = 0.5
    specular_strength = 0.5
    glass_opacity = 1.0
    edge_thickness = 0.08
}
```

## How It Works

The effect is composed of five layers rendered per window:

1. **Gaussian blur** — The background behind the window is sampled with padding and blurred via a two-pass (horizontal + vertical) Gaussian shader, run for 3 iterations.
2. **Bezel refraction** — An SDF-based edge strip refracts the blurred background using a convex lens profile.
3. **Chromatic aberration** — RGB channels are offset in the bezel zone for a prismatic look.
4. **Fresnel edge glow** — A subtle bright rim based on the cube of the bezel factor.
5. **Specular highlights + inner shadow** — Virtual light sources add depth at the top and side edges, with a soft shadow at the bottom-right.

The plugin integrates with Hyprland's render pass system as a `DECORATION_LAYER_BOTTOM` decoration, drawing before the window surface so the glass shows through transparent windows.

## Unloading

```bash
hyprctl plugin unload /path/to/hyprnux-liquid.so
```

## License

See repository for license details.
