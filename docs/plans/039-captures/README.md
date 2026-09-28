# Plan 039 UI captures

[Side-by-side comparison](comparison.png) shows the same synthetic UI states at
1.00× (left) and 1.10× (right). Individual native-resolution PNGs are in
`baseline/` and `candidate/`. The eleven views are Home, Launcher, Settings,
Bluetooth list, Bluetooth pairing, Wi-Fi Settings, System, Mac Status, Mac
Control, Pomodoro, and LED Gallery.

The baseline was recorded from the pre-039 `HEAD` source in a temporary checkout
with capture-only test hooks. The candidate was recorded from the 1.10× source.
The tests write draw commands when `CARDPUTER_UI_CAPTURE_DIR` points to an
existing directory; `scripts/render_ui_capture.py` renders them with the pinned
Font0 bitmap. For example:

```sh
mkdir -p /tmp/cardputer-ui
CARDPUTER_UI_CAPTURE_DIR=/tmp/cardputer-ui make test
python scripts/render_ui_capture.py /tmp/cardputer-ui
```

The PNG renderer approximates fractional M5GFX rasterization. It is useful for
checking layout and clipping; the final appearance and readability still require
a physical Cardputer Adv review.
