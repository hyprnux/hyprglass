// Embeds the built, standalone hyprglass-item-helper.so inside hyprglass.so,
// so a plugin manager that only installs this single output file still ships
// the protocol helper (see ItemHints.cpp: it is written to a memfd and
// dlopen'd from there at PLUGIN_INIT). GNU as, works with both gcc and clang
// as the driver.

.section .rodata
.global hyprglassItemHelperBlobStart
.balign 8
hyprglassItemHelperBlobStart:
.incbin "src/item-helper/hyprglass-item-helper.so"
.global hyprglassItemHelperBlobEnd
hyprglassItemHelperBlobEnd:
