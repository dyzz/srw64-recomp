"""Calibration of nonlinear registration against known geometric transforms."""
import unittest
from PIL import Image,ImageChops
from tools.hd_ai.tactical_registration_field import displacement,fit_field,place_field


class RegistrationFieldTests(unittest.TestCase):
    def samples(self,shift):
        return [{'center_source_px':[x,y],'dx_source_px':shift(x,y)[0],
                 'dy_source_px':shift(x,y)[1],'status':'sample_within_half_cell'}
                for y in (4,14,25,36) for x in (4,18,32,46,60)]

    def test_recovers_known_curved_field_between_measurements(self):
        expected=lambda x,y:(1+.0004*(x-32)**2+.0002*x*y,-1+.0003*y*y)
        fitted=fit_field(self.samples(expected),(0,0,64,40))
        for x,y in ((9,8),(27,31),(51,19)):
            for a,b in zip(displacement(fitted['coefficients'],x,y,64,40),expected(x,y)):
                self.assertAlmostEqual(a,b,places=8)

    def test_rejects_excessive_scale_even_when_residual_is_zero(self):
        with self.assertRaisesRegex(ValueError,'scales or shears'):
            fit_field(self.samples(lambda x,y:(.2*x,0)),(0,0,64,40))

    def test_rejects_excessive_displacement(self):
        with self.assertRaisesRegex(ValueError,'exceeds 24'):
            fit_field(self.samples(lambda x,y:(25,0)),(0,0,64,40))

    def test_edge_clamp_avoids_black_bands_when_sampling_outside_art(self):
        original=Image.new('RGB',(128,80),(40,80,120))
        field=fit_field(self.samples(lambda x,y:(-4,-3)),(0,0,64,40))
        out=place_field(original,field,(64,40))
        self.assertIsNone(ImageChops.difference(out,Image.new('RGB',out.size,(40,80,120))).getbbox())


if __name__=='__main__':unittest.main()
