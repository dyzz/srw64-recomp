"""Calibration checks for correlation diagnostics; these are not asset approvals."""
import random
import unittest

from PIL import Image, ImageFilter

from tools.hd_ai.tactical_map_audit import diagnose, local_shift


class TacticalMapAuditTests(unittest.TestCase):
    def source(self):
        rng=random.Random(3401)
        image=Image.frombytes('L',(96,96),bytes(rng.randrange(256) for _ in range(96*96)))
        return image.filter(ImageFilter.GaussianBlur(1))

    def shifted(self,source,dx,dy):
        target=Image.new('L',source.size,128)
        target.paste(source,(dx,dy))
        return target

    def test_known_shift_below_half_cell(self):
        source=self.source()
        result=local_shift(source,self.shifted(source,3,-2),(28,28,68,68))
        self.assertEqual((result['dx_source_px'],result['dy_source_px']),(3,-2))
        self.assertEqual(result['status'],'sample_within_half_cell')

    def test_known_shift_over_half_cell(self):
        source=self.source()
        result=local_shift(source,self.shifted(source,-10,1),(28,28,68,68))
        self.assertEqual((result['dx_source_px'],result['dy_source_px']),(-10,1))
        self.assertEqual(result['status'],'suspect_shift_over_half_cell')

    def test_search_boundary_does_not_claim_bounded_shift(self):
        source=self.source()
        result=local_shift(source,self.shifted(source,12,0),(28,28,68,68))
        self.assertTrue(result['search_boundary'])
        self.assertEqual(result['status'],'ambiguous_match')

    def test_wider_search_finds_large_shift_without_relaxing_half_cell_limit(self):
        source=self.source()
        result=local_shift(source,self.shifted(source,18,-3),(28,28,68,68),radius=24)
        self.assertEqual((result['dx_source_px'],result['dy_source_px']),(18,-3))
        self.assertFalse(result['search_boundary'])
        self.assertEqual(result['status'],'suspect_shift_over_half_cell')

    def test_flat_region_is_not_geometry_evidence(self):
        flat=Image.new('L',(96,96),30)
        self.assertEqual(local_shift(flat,flat,(28,28,68,68))['status'],'insufficient_texture')

    def test_protected_pixels_are_excluded(self):
        source=self.source().convert('RGB')
        result=diagnose(source,source,Image.new('L',source.size,255))
        self.assertFalse(result['acceptance_proven'])
        self.assertEqual(result['reliable_samples'],0)
        self.assertEqual(result['static_cells_compared'],0)
        self.assertTrue(all(s['status']=='excluded_dynamic_or_border' for s in result['samples']))


if __name__=='__main__': unittest.main()
