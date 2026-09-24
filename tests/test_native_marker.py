from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
from recomp.model5600.prepare_native_marker import validate


class NativeMarkerPackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT/'rom.z64').exists():
            raise unittest.SkipTest('Local original ROM required for asset identity checks')
        cls.storage = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.storage.cleanup)
        cls.pack = Path(cls.storage.name)/'authored'
        subprocess.run([sys.executable, str(ROOT/'tools/recomp/model5600/prepare_native_marker.py'),
                        '--output', str(cls.pack)], check=True, stdout=subprocess.DEVNULL)

    def copy_pack(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        path = Path(temporary.name)/'pack'
        shutil.copytree(self.pack,path)
        return path

    def test_authored_geometry_and_gpu_payload_agree(self):
        report = validate(self.pack)
        self.assertGreater(report['triangles'], 500)
        mesh = json.loads((self.pack/'mesh.json').read_text())
        self.assertEqual([min(p[1] for p in mesh['positions']),max(p[1] for p in mesh['positions'])],[-12,12])
        self.assertLessEqual(max(abs(v) for p in mesh['positions'] for v in (p[0], p[2])), 5)
        self.assertTrue(any(abs(v-round(v))>1e-3 for p in mesh['positions'] for v in p))
        # The original eight faces survive as flat facets: each face normal is shared by
        # its triangle's three corners; bevel and corner normals vary vertex to vertex.
        normals = [tuple(n) for n in mesh['normals']]
        flat = [n for n in set(normals) if normals.count(n) >= 3]
        self.assertEqual(len(flat), 8)
        self.assertEqual(sorted(n[1] > 0 for n in flat), [False]*4 + [True]*4)

    def test_stale_or_incomplete_asset_pack_is_rejected(self):
        path = self.copy_pack()
        data = bytearray((path/'vertices.bin').read_bytes()); data[0] ^= 1
        (path/'vertices.bin').write_bytes(data)
        with self.assertRaisesRegex(ValueError,'asset drift'):
            validate(path)
        manifest = json.loads((path/'manifest.json').read_text())
        manifest['files'] = {}
        (path/'manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError,'Incomplete'):
            validate(path)

    def test_refreshed_hash_cannot_hide_preview_gpu_mismatch(self):
        path = self.copy_pack()
        mesh = json.loads((path/'mesh.json').read_text())
        mesh['positions'][0][0] += 1
        (path/'mesh.json').write_text(json.dumps(mesh))
        manifest = json.loads((path/'manifest.json').read_text())
        manifest['files']['mesh.json'] = hashlib.sha256((path/'mesh.json').read_bytes()).hexdigest()
        (path/'manifest.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(ValueError,'Preview and GPU vertices differ'):
            validate(path)


if __name__ == '__main__':
    unittest.main()
