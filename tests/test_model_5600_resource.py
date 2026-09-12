from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'src'))
sys.path.insert(0, str(ROOT/'tools/recomp'))
from srw64_rom.resources import ResourceTable, lz_encode, lz_decode
from build_model_5600_rom import build_resource
from prepare_model_5600_highpoly import make_mesh, SOLID_COMMANDS


def decode_positions(data, base):
    """Bounded independent traversal of segment-4 lists and scale matrices."""
    def relative(address):
        assert address >> 24 == 4
        physical = base+(address & 0xFFFFFF)
        offset = physical-base
        assert 0 <= offset < len(data)
        return offset
    pc = struct.unpack_from('>I', data, 20)[0]
    returns, scales, cache, faces = [], [1.0], {}, []
    for _ in range(1000):
        a, b = struct.unpack_from('>II', data, pc)
        op = a >> 24
        if op == 0xDE:
            returns.append(pc+8)
            pc = relative(b)
            continue
        if op == 0xDF:
            if not returns:
                assert len(scales) == 1
                return faces
            pc = returns.pop()
            continue
        if op == 0xDA:
            offset = relative(b)
            scale = struct.unpack_from('>h', data, offset)[0]+struct.unpack_from('>H', data, offset+32)[0]/65536
            assert a == 0xDA380000
            scales.append(scales[-1]*scale)
        elif op == 0xD8:
            assert b == 64 and len(scales) > 1
            scales.pop()
        elif op == 1:
            count = (a >> 12) & 255
            first = ((a >> 1) & 127)-count
            assert 0 <= first < first+count <= 32
            offset = relative(b)
            for i in range(count):
                cache[first+i] = tuple(v*scales[-1] for v in struct.unpack_from('>hhh', data, offset+16*i))
        elif op in (5, 6):
            for word in ((a, b) if op == 6 else (a,)):
                faces.append(tuple(cache[(word >> shift & 255)//2] for shift in (16, 8, 0)))
        pc += 8
    raise AssertionError('Display list did not return')


class Model5600ResourceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not (ROOT/'rom.z64').exists():
            raise unittest.SkipTest('Local baseline ROM required for resource integration test')
        cls.original, _ = ResourceTable((ROOT/'rom.z64').read_bytes()).extract(5600)
        cls.replacement, cls.report = build_resource(cls.original)

    def test_relocated_lists_draw_exact_mesh_at_multiple_runtime_addresses(self):
        original_faces = decode_positions(self.original, 0x100000)
        mesh = make_mesh()
        expected = [tuple(tuple(mesh['positions'][i]) for i in f) for f in mesh['faces']]
        for address in (0x100000, 0x2BDE68, 0x380000):
            with self.subTest(address=address):
                faces = decode_positions(self.replacement, address)
                self.assertEqual(len(faces), 104)
                self.assertEqual(faces[:8], original_faces[:8])
                self.assertEqual(faces[8:], expected)

    def test_only_solid_commands_change_in_original_container(self):
        allowed = {o+i for o in SOLID_COMMANDS for i in range(8)}
        actual = {i for i, (a, b) in enumerate(zip(self.original, self.replacement)) if a != b}
        self.assertLessEqual(actual, allowed)
        self.assertEqual(self.original[0x1B40:], self.replacement[0x1B40:len(self.original)])

    def test_compression_roundtrip_and_wrong_identity_rejection(self):
        encoded = lz_encode(self.replacement)
        decoded, consumed = lz_decode(encoded, len(self.replacement))
        self.assertEqual(decoded, self.replacement)
        self.assertEqual(consumed, len(encoded))
        damaged = bytearray(self.original)
        damaged[0x1888] ^= 1
        with self.assertRaisesRegex(RuntimeError, 'Unreviewed original marker'):
            build_resource(bytes(damaged))
