{
  lib,
  stdenv,
  pkg-config,
  wayland-scanner,
  wayland,
  pixman,
  libdrm,
  hyprland,
  version ? "1.1.0",
}:

stdenv.mkDerivation (finalAttrs: {
  pluginName = "hyprglass";
  version = version;
  name = "hyprglass-${version}";

  src = ../.;

  nativeBuildInputs = [
    pkg-config
    wayland-scanner
  ];

  buildInputs = [
    hyprland
    wayland
    pixman
    libdrm
  ]
  ++ (hyprland.buildInputs or [ ])
  ++ (hyprland.propagatedBuildInputs or [ ]);

  makeFlags = [
    "HYPRGLASS_VERSION=${finalAttrs.version}"
    "WAYLAND_SCANNER=${lib.getBin wayland-scanner}/bin/wayland-scanner"
  ];

  installPhase = ''
    runHook preInstall
    mkdir -p $out/lib
    cp hyprglass.so $out/lib/libhyprglass.so
    runHook postInstall
  '';

  meta = {
    homepage = "https://github.com/hyprnux/hyprglass";
    description = "Liquid Glass inspired plugin for Hyprland";
    license = lib.licenses.bsd3;
    platforms = lib.platforms.linux;
  };
})
