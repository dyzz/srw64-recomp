import unittest
from PIL import Image, ImageDraw
from tools.hd_ai.tactical_colony_layers import body_layer


class ColonyLayerRegistrationTest(unittest.TestCase):
    def test_rotating_panel_cannot_change_body_scale_or_axis(self):
        # Synthetic registration fixture: a panel reaches the old anchor strip.
        image = Image.new('RGBA', (600, 400))
        draw = ImageDraw.Draw(image)
        draw.rectangle((80, 150, 503, 229), fill='gray')
        draw.polygon(((180, 150), (478, 35), (478, 60), (180, 170)), fill='white')
        _, fit = body_layer(image)
        a = fit['output_to_generated_affine']
        self.assertEqual(a[0], a[4])
        self.assertAlmostEqual(a[4] * 96 + a[5], 190)
        self.assertAlmostEqual(a[0] * 36 + a[2], 80)


if __name__ == '__main__':
    unittest.main()
