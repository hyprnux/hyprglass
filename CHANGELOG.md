
## [v0.2.5](https://github.com/Hyprnux/hyprglass/compare/v0.2.4...v0.2.5) - 2026-03-09

### Bug Fixes

* preset configuration was not correctly picked up ([#11](https://github.com/Hyprnux/hyprglass/issues/11))


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

