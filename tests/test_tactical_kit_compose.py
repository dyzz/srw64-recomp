"""Calibrate post-generation registration, blending and exact palette protection."""
import unittest
import struct
import json
import tempfile
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

from tools.hd_ai.tactical_kit_compose import apply_dense_review, assemble, fit_samples, load_landmark_review, place, restore_protected, seam_mask, sha, weight_image
from tools.hd_ai.tactical_colony_pack import colony_cells, colony_instances, frame_box


class TacticalKitComposeTests(unittest.TestCase):
    def samples(self):
        return [{'center_source_px':[x+32,y+32], 'dx_source_px':.02*x+2,
                 'dy_source_px':-.01*y-3, 'status':'sample_within_half_cell'}
                for x in (20,90,160,230) for y in (20,80,140,200)]

    def test_fit_uses_crop_origin_and_excludes_dynamic_matches(self):
        samples=self.samples()+[{'center_source_px':[100,100],'status':'excluded_dynamic_or_border'}]
        fit=fit_samples(samples,(32,32,288,272))
        self.assertEqual(fit['status'],'fitted_sparse_samples')
        for actual,expected in zip(fit['x']+fit['y'],[1.02,2,.99,-3]):
            self.assertAlmostEqual(actual,expected)

    def test_uncertain_fit_is_not_a_pass(self):
        fit=fit_samples(self.samples()[:4],(32,32,288,272))
        self.assertEqual(fit['status'],'insufficient_evidence')
        self.assertEqual(fit['x'],[1.,0.])

    def test_dense_review_rejects_changed_inputs_before_applying_samples(self):
        with tempfile.TemporaryDirectory() as tmp:
            kit=Path(tmp)
            (kit/'source.png').write_bytes(b'original source')
            (kit/'output.png').write_bytes(b'generated artwork')
            box=[32,32,288,272];identifier='map-001-00-whole'
            spec={'map':1,'paint_box':box,'source':'source.png','windows':[],
                  'whole':{'output':'output.png'}}
            row={'id':identifier,'map':1,'box':box,'source_sha256':sha(kit/'source.png'),
                 'generated_sha256':sha(kit/'output.png'),'raw':{'samples':self.samples()},
                 'fit':fit_samples(self.samples(),box)}
            path=kit/'review.json'
            path.write_text(json.dumps({'schema':'srw64.tactical-dense-registration-review.v1','rows':[row]}))
            audit={identifier:{'sha256':row['generated_sha256'],'samples':[]}}
            result=apply_dense_review(kit,{1:spec},audit,path,[identifier])
            self.assertEqual(result[identifier]['samples'],self.samples())
            self.assertEqual(audit[identifier]['samples'],[])
            (kit/'output.png').write_bytes(b'new generated revision')
            with self.assertRaisesRegex(ValueError,'Stale dense review inputs'):
                apply_dense_review(kit,{1:spec},audit,path,[identifier])

    def test_fit_rejects_unbounded_distortion(self):
        samples=self.samples()
        for s in samples:
            s['dx_source_px']=.2*(s['center_source_px'][0]-32)
        self.assertEqual(fit_samples(samples,(32,32,288,272))['status'],'inconsistent_fit')

    def test_known_shift_is_undone_and_edges_do_not_turn_black(self):
        source=Image.new('RGB',(64,64),(80,100,120))
        ImageDraw.Draw(source).rectangle((24,20,40,36),fill=(200,150,100))
        generated=Image.new('RGB',source.size,(80,100,120));generated.paste(source,(3,-2))
        fitted=place(generated,{'x':[1,3],'y':[1,-2]},source.size,scale=1)
        self.assertIsNone(ImageChops.difference(fitted.crop((8,8,56,56)),source.crop((8,8,56,56))).getbbox())
        self.assertEqual(fitted.getpixel((0,0)),(80,100,120))

    def test_protected_pixels_exact_despite_feather(self):
        flat=Image.new('RGB',(24,24),(11,77,135));painted=Image.new('RGB',flat.size,(220,50,10))
        mask=Image.new('L',flat.size);ImageDraw.Draw(mask).rectangle((8,8,15,15),fill=255)
        result=restore_protected(painted,flat,mask)
        self.assertIsNone(ImageChops.difference(result.crop((8,8,16,16)),flat.crop((8,8,16,16))).getbbox())
        self.assertEqual(result.getpixel((0,0)),painted.getpixel((0,0)))
        self.assertNotEqual(result.getpixel((7,10)),painted.getpixel((7,10)))

    def test_piecewise_registration_aligns_two_shifted_boundaries_without_gaps(self):
        painted=Image.new('RGB',(64,32),(70,80,90))
        d=ImageDraw.Draw(painted);d.line((21,0,21,31),fill='white');d.line((41,0,41,31),fill='white')
        out=place(painted,{'x':[1,0],'y':[1,0]},painted.size,scale=1,
                  x_landmarks=[[0,0],[20,21],[40,41],[64,64]])
        self.assertEqual([x for x in range(64) if out.getpixel((x,16))[0]>230],[20,40])
        self.assertGreater(min(out.getpixel((x,16))[0] for x in range(64)),0)

    def test_landmark_review_rejects_stale_art_and_folded_controls(self):
        with tempfile.TemporaryDirectory() as tmp:
            kit=Path(tmp);(kit/'source').write_bytes(b'source');(kit/'art').write_bytes(b'art')
            spec={'map':1,'paint_box':[0,0,64,32],'source':'source','windows':[],
                  'whole':{'output':'art'}}
            row={'id':'map-001-00-whole','map':1,'box':spec['paint_box'],
                 'source_sha256':sha(kit/'source'),'generated_sha256':sha(kit/'art'),
                 'x_landmarks':[[0,0],[20,21],[40,41],[64,64]]}
            path=kit/'review.json'
            def save():path.write_text(json.dumps({'schema':'srw64.tactical-landmark-registration.v1','rows':[row]}))
            save();self.assertIn(row['id'],load_landmark_review(kit,{1:spec},path))
            row['x_landmarks'][2]=[19,18];save()
            with self.assertRaisesRegex(ValueError,'fold or excessively'):load_landmark_review(kit,{1:spec},path)
            row['x_landmarks']=[[0,0],[64,64]];save();(kit/'art').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'Stale landmark'):load_landmark_review(kit,{1:spec},path)

    def test_overlapping_windows_keep_whole_colour_and_fill_canvas(self):
        base=Image.new('RGB',(128,96),(30,70,110))
        result=assemble(base,[((0,0,24,24),Image.new('RGB',(96,96),(150,170,190))),
                              ((8,0,32,24),Image.new('RGB',(96,96),(70,90,130)))])
        self.assertIsNone(ImageChops.difference(result,base).getbbox())
        weight=weight_image((96,96),(0,0,24,24),(32,24))
        self.assertGreater(weight.getpixel((0,48)),weight.getpixel((95,48)))

    def test_colony_gutters_are_excluded_from_frame_crops(self):
        self.assertEqual(frame_box([80,64,144,112],(304,112),(2065,761)),(543,435,978,761))
        first=frame_box([0,0,64,48],(304,112),(2065,761))
        second=frame_box([80,0,144,48],(304,112),(2065,761))
        self.assertGreater(second[0],first[2])

    def test_seam_does_not_duplicate_displaced_outline(self):
        old=Image.new('RGB',(256,192),(80,80,80));new=old.copy()
        ImageDraw.Draw(old).rectangle((110,0,118,191),fill=(240,240,240))
        ImageDraw.Draw(new).rectangle((138,0,146,191),fill=(240,240,240))
        for vertical in (True,False):
            a=old if vertical else old.transpose(Image.Transpose.TRANSPOSE)
            b=new if vertical else new.transpose(Image.Transpose.TRANSPOSE)
            result=Image.composite(b,a,seam_mask(a,b,vertical))
            if not vertical:result=result.transpose(Image.Transpose.TRANSPOSE)
            self.assertEqual(sum(result.getpixel((x,96))[0]>160 for x in range(256)),9)

    def test_seam_quilting_covers_first_edges_and_preserves_whole_colour(self):
        base=Image.new('RGB',(128,128),(30,70,110))
        pieces=[((x,y,x+20,y+20),Image.new('RGB',(80,80),(150+x,170,190)))
                for y in (0,12) for x in (0,12)]
        result=assemble(base,list(reversed(pieces)),blend_mode='seam')
        self.assertIsNone(ImageChops.difference(result,base).getbbox())

    def test_colony_layout_records_flip_and_exact_atlas_offsets(self):
        tiles=[x+y for y in (0,0x20,0x40) for x in (0x10a,0x10c,0x10e,0x200)]
        layout=struct.pack('>4H',0,0,8,6)+b''.join(struct.pack('>BBH',0x40 if i==0 else 0,0,t) for i,t in enumerate(tiles))
        data={'atlas':6229,'mode':1,'layout_bytes':layout}
        cells=colony_cells(data)
        self.assertEqual(len(cells),12)
        self.assertEqual(cells[-1]['map_xy'],[48,32])
        self.assertEqual(cells[-1]['frame_xy'],[48,32])
        self.assertTrue(cells[0]['flip_x'])
        self.assertFalse(cells[0]['flip_y'])
        self.assertEqual(colony_cells(dict(data,mode=0)),[])
        with self.assertRaisesRegex(ValueError,'flipped'):
            colony_instances(data)
        data['layout_bytes']=layout[:8]+b''.join(struct.pack('>BBH',0,0,t) for t in tiles)
        self.assertEqual(colony_instances(data),[[0,0]])
        data['layout_bytes']=data['layout_bytes'][:-4]+struct.pack('>BBH',0,0,0)
        with self.assertRaisesRegex(ValueError,'Incomplete'):
            colony_instances(data)


if __name__=='__main__':
    unittest.main()
