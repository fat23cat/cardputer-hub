# Plan 040 UI captures

[Three-way comparison](comparison.png) shows the same eleven synthetic UI
states at 1.00×, 1.10×, and 1.20×. The first two columns come from
[Plan 039's captures](../039-captures/README.md); the 1.20× native-resolution
PNGs are in `candidate/`.

Native tests recorded draw commands with `CARDPUTER_UI_CAPTURE_DIR`, then
`scripts/render_ui_capture.py` rendered Font0. These PNGs approximate M5GFX
fractional rasterization. Final readability requires a physical display review.
