
## [v0.8.0](https://github.com/Hyprnux/hyprglass/compare/v0.7.0...v0.8.0) - 2026-09-03

### Bug Fixes

* validate hg.config keys and report errors at the caller ([#71](https://github.com/Hyprnux/hyprglass/issues/71))
* keep glass on internal-fullscreen windows, recover on fullscreen exit ([#54](https://github.com/Hyprnux/hyprglass/issues/54))
* size layer FBOs from the framebuffer, not the monitor transform
* own event listeners in plugin state so unload unregisters them

### Build System

* rebuild objects when headers change
* update hyprland compatibility to v0.56.2
* update hyprland compatibility to v0.56.1

### Features

* add support for ext-background-effect-v1 (wayland) with alpha fallback ([#61](https://github.com/Hyprnux/hyprglass/issues/61))
* per-layer live_resample override for layer glass ([#59](https://github.com/Hyprnux/hyprglass/issues/59))
* re-rendering layer background when window content behind it change ([#59](https://github.com/Hyprnux/hyprglass/issues/59))

### Performance Improvements

* mark layer glass dirty from actual commit damage only ([#59](https://github.com/Hyprnux/hyprglass/issues/59))


## [v0.7.0](https://github.com/Hyprnux/hyprglass/compare/v0.6.4...v0.7.0) - 2026-07-20

### Bug Fixes

* don't dim glass by inactive alpha and opacity rules
* use source framebuffer format for layer temp FBO in HDR mode
* set noblur on glassed windows, glass replaces hyprland blur ([#46](https://github.com/Hyprnux/hyprglass/issues/46))
* use MONITORID instead of Monitor::CMonitor, unavailable before hyprland 0.56 ([#44](https://github.com/Hyprnux/hyprglass/issues/44))
* relax version check to compare ABI suffix only
* qualify CMonitor with Monitor:: namespace
* wrong viewport restore after blur on transformed monitors ([#41](https://github.com/Hyprnux/hyprglass/issues/41))
* match dynamic window tags for preset selection ([#45](https://github.com/Hyprnux/hyprglass/issues/45))

### Build System

* update hyprland compatibility to v0.56.0

### Chores

* **release:** v0.7.0 [skip ci]

### Documentation

* add troubleshooting section

### Features

* compatibility hyprland 0.56
* add HYPRGLASS_SKIP_VERSION_CHECK escape hatch ([#44](https://github.com/Hyprnux/hyprglass/issues/44))


## [v0.6.4](https://github.com/Hyprnux/hyprglass/compare/v0.6.3...v0.6.4) - 2026-06-12

### Build System

* update hyprland compatibility to v0.55.4

### Chores

* **release:** v0.6.4 [skip ci]


## [v0.6.3](https://github.com/Hyprnux/hyprglass/compare/v0.6.2...v0.6.3) - 2026-06-08

### Build System

* update hyprland compatibility to v0.55.3

### CI/CD

* **hyprpm:** correctly pin to hyprland versions ([#38](https://github.com/Hyprnux/hyprglass/issues/38))

### Chores

* **release:** v0.6.3 [skip ci]


## [v0.6.2](https://github.com/Hyprnux/hyprglass/compare/v0.6.1...v0.6.2) - 2026-05-21

### Bug Fixes

* keep layer mask threshold stable during fade

### Chores

* **release:** v0.6.2 [skip ci]


## [v0.6.1](https://github.com/Hyprnux/hyprglass/compare/v0.6.0...v0.6.1) - 2026-05-20

### Bug Fixes

* initialize presets before validation
* support legacy string config values

### Chores

* **release:** v0.6.1 [skip ci]


## [v0.6.0](https://github.com/Hyprnux/hyprglass/compare/v0.5.0...v0.6.0) - 2026-05-19

### Bug Fixes

* clean up decoration lifetime on unload
* stabilize Hyprland 0.55 plugin integration

### Build System

* update hyprland compatibility to v0.55.2

### Chores

* **release:** v0.6.0 [skip ci]

### Documentation

* add Lua config example
* **configuration:** reorganize docs + enhance lua/conf doc ([#16](https://github.com/Hyprnux/hyprglass/issues/16))

### Features

* **configuration:** improve lua configuration using lua functions ([#16](https://github.com/Hyprnux/hyprglass/issues/16))


## [v0.5.0](https://github.com/Hyprnux/hyprglass/compare/v0.4.1...v0.5.0) - 2026-05-06

### Bug Fixes

* fading workspace  animations were not fading the glass layer ([#24](https://github.com/Hyprnux/hyprglass/issues/24))

### Chores

* **release:** v0.5.0 [skip ci]

### Features

* add ability to enable/disable hyprglass effect per-window ([#23](https://github.com/Hyprnux/hyprglass/issues/23))


## [v0.4.1](https://github.com/Hyprnux/hyprglass/compare/v0.4.0...v0.4.1) - 2026-03-31

### Bug Fixes

* when workspace animation occurs and no windows were on workspace, effect was not redrawn (useful when background animation occur on workspace changes)

### Chores

* **release:** v0.4.1 [skip ci]


## [v0.4.0](https://github.com/Hyprnux/hyprglass/compare/v0.3.1...v0.4.0) - 2026-03-31

### Bug Fixes

* correct redraw artifact on multi-monitor setup
* some low opacity layers on XRGB monitors where shown without the effect

### Chores

* **release:** v0.4.0 [skip ci]

### Features

* **layers:** add namespace_mask_thresholds for better shadow handling + fix some config parsing issues


## [v0.3.1](https://github.com/Hyprnux/hyprglass/compare/v0.3.0...v0.3.1) - 2026-03-30

### Build System

* update hyprland compatibility to v0.54.3

### Chores

* **release:** v0.3.1 [skip ci]


## [v0.3.0](https://github.com/Hyprnux/hyprglass/compare/v0.2.7...v0.3.0) - 2026-03-25

### Chores

* **release:** v0.3.0 [skip ci]

### Features

* handle layer decoration - BETA ([#6](https://github.com/Hyprnux/hyprglass/issues/6))

### Performance Improvements

* conditionaly make half-res blur if blur_strenght is sufficient enough
* reduce GPU overhead for layer glass effect, still not perfect ([#6](https://github.com/Hyprnux/hyprglass/issues/6))


## [v0.2.7](https://github.com/Hyprnux/hyprglass/compare/v0.2.6...v0.2.7) - 2026-03-11

### Build System

* improved the makefile to allow for parallel builds + fix linker flag

### Chores

* **release:** v0.2.7 [skip ci]


## [v0.2.6](https://github.com/Hyprnux/hyprglass/compare/v0.2.5...v0.2.6) - 2026-03-11

### Build System

* update hyprland compatibility to v0.54.2

### Chores

* **release:** v0.2.6 [skip ci]


## [v0.2.5](https://github.com/Hyprnux/hyprglass/compare/v0.2.4...v0.2.5) - 2026-03-09

### Bug Fixes

* preset configuration was not correctly picked up ([#11](https://github.com/Hyprnux/hyprglass/issues/11))

### Chores

* **release:** v0.2.5 [skip ci]


## [v0.2.4](https://github.com/Hyprnux/hyprglass/compare/v0.2.3...v0.2.4) - 2026-03-04

### Build System

* update hyprland compatibility to v0.54.1

### Chores

* **release:** v0.2.4 [skip ci]


## [v0.2.3](https://github.com/Hyprnux/hyprglass/compare/v0.2.2...v0.2.3) - 2026-03-02

### Bug Fixes

* need to always resample to catch real time background change, cheap GPU overhead, but no way to do otherwise as of now

### Chores

* add BSD 3-Clause license
* remove some standard uniform names (already resolved internally by CShader since Hyprland 0.54)
* **release:** v0.2.3 [skip ci]


## [v0.2.2](https://github.com/Hyprnux/hyprglass/compare/v0.2.1...v0.2.2) - 2026-03-02

### Bug Fixes

* refactor shader creation for 0.54 refactored shader logic
* refactor callbacks for new event bus listeners

### Build System

* update hyprland compatibility to v0.54.0

### Chores

* **release:** v0.2.2 [skip ci]


## [v0.2.1](https://github.com/Hyprnux/hyprglass/compare/v0.2.0...v0.2.1) - 2026-02-18

### Bug Fixes

* **ci:** build tag from hyprland version bump was not picked up for release

### Build System

* update hyprland compatibility to v0.53.3

### Chores

* **release:** v0.2.1 [skip ci]


## [v0.2.0](https://github.com/Hyprnux/hyprglass/compare/v0.1.0...v0.2.0) - 2026-02-18

### Chores

* **release:** v0.2.0 [skip ci]

### Features

* settings improvments, with presets and built-in presets


## v0.1.0 - 2026-02-16

### Bug Fixes

* area was not damaged correctly  when moving while tiled layout
* remove some optimization causing noise when rendering blur and moving window (not worth it)
* gpu infinite render
* border radius
* working with shadow enable, not yet with shadow disabled

### CI/CD

* pipelines + docs

### Chores

* **release:** v0.1.0 [skip ci]

### Code Refactoring

* better file separation, renaming
* remove unused code and fix bounding box
* attempt with glass shader using SDF bezel refraction and poisson blur

### Features

* improve settings
* almost perfect
* more like apple variant, magnifying variant, seemd to be more beautiful
* add meniscus dispersion, color, and border rim to glasss shader (attempt to make it more apple-like, cool effects but not a big usable success)"
* tweaking little bit different approach
* add color_tint effect
* split corner/bezel SDF, add multi-pass blur and inner shadow (in order to avoid weird corners, but not really a success yet)
* add UV padding and remove wave distorsion
* draft working version of shader for transparent windows

### Performance Improvements

* massive gpu usage improvments, resample only when really needed
* shared blur framebuffers, remove raw texture sampler
* half-res blur pipeline, linear sampling, shadows

