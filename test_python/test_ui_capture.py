import unittest

from scripts.render_ui_capture import draw_text


class Raster:
    def __init__(self):
        self.pixels = {}

    def rectangle(self, bounds, fill):
        left, top, right, bottom = bounds
        for y in range(top, bottom + 1):
            for x in range(left, right + 1):
                self.pixels[x, y] = fill


class UiCaptureTests(unittest.TestCase):
    def setUp(self):
        self.font = bytearray(1280)
        self.font[ord("A") * 5 + 4] = 0x80

    def test_fractional_font_advances_by_seven_and_is_nine_pixels_high(self):
        raster = Raster()
        draw_text(raster, self.font, "AA", 0, 0, 1.2, "ink", "bone")
        self.assertEqual(set(raster.pixels), {(x, y) for x in range(14) for y in range(9)})
        self.assertEqual({point for point, color in raster.pixels.items() if color == "ink"},
                         {(4, 8), (11, 8)})

    def test_clip_limits_both_foreground_and_opaque_background(self):
        raster = Raster()
        draw_text(raster, self.font, "AA", 0, 0, 1.2, "bone", "ink", (5, 7, 7, 2))
        self.assertEqual(set(raster.pixels), {(x, y) for x in range(5, 12) for y in range(7, 9)})
        self.assertEqual(raster.pixels[5, 8], "ink")
        self.assertEqual(raster.pixels[6, 8], "ink")
        self.assertEqual(raster.pixels[11, 8], "bone")


if __name__ == "__main__":
    unittest.main()
