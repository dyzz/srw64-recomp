"""Compile the host's projection helper without the GPU and exercise scrolling/zoom."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


class NativeMapGeometryTests(unittest.TestCase):
    def test_projection_clips_uv_without_stretching_and_tracks_zoom(self):
        compiler=shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        root=Path(__file__).resolve().parents[1]
        source=r'''
#include "src/host/native_map_geometry.hpp"
#include <cassert>
#include <cmath>
void eq(float a, float b) { assert(std::abs(a-b)<0.0001f); }
int main() {
    using srw64::hdmap::project_overlay;
    float rect[]={0,0,320,240}, uv[]={.25f,.25f,.5625f,.5625f}, r[4], t[4];
    // Camera is at (256,192): sprite extends 16 pixels beyond the left edge.
    assert(project_overlay(rect,uv,1024,768,240,208,64,48,r,t));
    eq(r[0],0); eq(r[1],16); eq(r[2],48); eq(r[3],64);
    eq(t[0],.25f); eq(t[1],0); eq(t[2],1); eq(t[3],1);
    assert(!project_overlay(rect,uv,1024,768,100,208,64,48,r,t));
    assert(!project_overlay(rect,uv,1024,768,256,500,64,48,r,t));
    // Zoom to twice the screen scale; clipping still chooses half the texture.
    uv[2]=.40625f; uv[3]=.40625f;
    assert(project_overlay(rect,uv,1024,768,384,288,64,48,r,t));
    eq(r[0],256); eq(r[1],192); eq(r[2],320); eq(r[3],240);
    eq(t[0],0); eq(t[1],0); eq(t[2],.5f); eq(t[3],.5f);
    uv[2]=uv[0];
    assert(!project_overlay(rect,uv,1024,768,384,288,64,48,r,t));
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder);(path/'test.cpp').write_text(source)
            subprocess.run([compiler,'-std=c++17','-I',str(root),str(path/'test.cpp'),'-o',str(path/'test')],check=True,capture_output=True)
            subprocess.run([str(path/'test')],check=True,capture_output=True)


if __name__=='__main__':
    unittest.main()
