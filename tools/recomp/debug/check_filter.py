#!/usr/bin/env python3
"""Verify RetroArch shader presets and bezels (docs/native/bezels-and-filters.md).

Needs librashader beside the host (tools/recomp/toolchain/fetch_librashader.py) and
RetroArch's slang shaders and overlays where RetroArch keeps them. On the title screen:
a CRT preset at the original 240 lines and at the window's pixels, a preset that fails
to compile, a multi-pass preset, back to none; then a bezel around the 4:3 picture.
Screenshots go to the run directory."""
import argparse
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

RA = Path.home() / 'Library/Application Support/RetroArch'
SHADERS = RA / 'shaders/shaders_slang'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--preset', default=str(SHADERS / 'crt/crt-lottes.slangp'))
parser.add_argument('--multipass', default=str(SHADERS / 'crt/crt-guest-advanced.slangp'))
parser.add_argument('--bezel', default=str(RA / 'overlays/borders/snes-lttp.cfg'))
parser.add_argument('--vulkan', metavar='LIBRASHADER', help='run RT64 on Vulkan (MoltenVK on a Mac) with this librashader build')
args = parser.parse_args()
env = {'SRW64_GRAPHICS_API': 'vulkan', 'SRW64_LIBRASHADER': args.vulkan} if args.vulkan else None
s = Session.launch(language='zh-Hans', images='hd', reuse_build=args.reuse_build, env=env)
print('RUN', s.run, flush=True)
checks = []
def status():
    return s.client.call('status')
def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run/'filter-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2)+'\n')
    assert passed, (name, state)
    print(name, 'PASS', flush=True)
def shot(name):
    s.client.call('screenshot', path=str(s.run/name))
def settled(timeout=60):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        f = status()['filter']
        if not f['loading']:
            return f
        time.sleep(.2)
    raise AssertionError('preset still compiling')

s.wait(vi=600)
for _ in range(8):
    run_keys(s.client, [{'press': 'return'}])
    time.sleep(.5)
    try:
        s.wait(title_major=3, timeout=4)
        break
    except Exception:
        pass
time.sleep(2.5)
f = status()['filter']
check('available', f['available'] and not f['preset'], f)
shot('filter-none.png')

s.client.call('settings', filter=args.preset, filter_scale=1)
time.sleep(1)
f = settled()
check('preset-240', f['preset'] == args.preset and not f['error'], f)
time.sleep(1)
shot('filter-240.png')
s.client.call('settings', filter_scale=0)
time.sleep(1)
shot('filter-window.png')
s.client.call('settings', filter=args.multipass, filter_scale=2)
time.sleep(1)
f = settled(120)
check('multipass', f['preset'] == args.multipass and not f['error'], f)
time.sleep(1)
shot('filter-multipass.png')
broken = s.run / 'broken.slangp'
broken.write_text('shaders = 1\nshader0 = missing.slang\n')
s.client.call('settings', filter=str(broken))
time.sleep(1)
f = settled()
check('broken-reported', f['error'] and f['preset'] != str(broken), f)
s.client.call('settings', filter='')
time.sleep(1)
f = settled()
check('off', not f['preset'], f)

s.client.call('settings', aspect='4:3', bezel=args.bezel)
time.sleep(1.5)
shot('bezel.png')
check('bezel-drawn', 'img' in json.dumps(s.client.call('ui.tree')), {})

# The same through the settings window: the General page's pickers.
def ids():
    found = []
    def walk(node):
        if isinstance(node, dict):
            if isinstance(node.get('id'), str) and node['id']:
                found.append(node['id'])
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)
    walk(s.client.call('ui.tree'))
    return found
def click(id):
    s.client.call('ui.click', id=id)
    time.sleep(.6)
s.client.call('settings', bezel='', filter='')
s.client.call('pad', press='view', hold_ms=100)
time.sleep(1)
click('settings-page:general')
click('browse:filter')
roots = [i for i in ids() if i.startswith('browse-dir:')]
# Built in (beside the host), the player's own, then RetroArch's.
check('filter-roots', len(roots) >= 3 and roots[0].endswith('/filters') and roots[1].endswith('/filters')
      and roots[2].endswith('shaders_slang') and any(i.startswith('open-folder:') for i in ids()), {'roots': roots})
mine = Path(roots[1].split(':', 1)[1])
check('readme', (mine / 'README.txt').exists() or not s.local(), {'mine': str(mine)})
click(roots[0])
click(roots[0] + '/crt')
builtin = roots[0].split(':', 1)[1] + '/crt/zfast-crt.slangp'
check('builtin-listed', 'browse-pick:' + builtin in ids(), {})
click('browse-pick:' + builtin)
f = settled()
check('builtin-picked', f['preset'] == builtin and not f['error'], f)
shot('builtin-zfast.png')
click('browse:filter')
click(roots[2])
click(roots[2] + '/crt')
pick = 'browse-pick:' + str(SHADERS / 'crt/crt-easymode.slangp')
check('filter-listed', pick in ids(), {})
shot('picker-filter.png')
click(pick)
f = settled()
check('filter-picked', f['preset'] == str(SHADERS / 'crt/crt-easymode.slangp') and not f['error'], f)
click('filter-scale:1')
s.client.call('settings', aspect='4:3')
time.sleep(.8)
click('browse:bezel')
roots = [i for i in ids() if i.startswith('browse-dir:')]
check('bezel-roots', len(roots) >= 2 and roots[0].endswith('/bezels') and roots[-1].endswith('overlays'), {'roots': roots})
click(roots[-1])
click(roots[-1] + '/borders')
pick = 'browse-pick:' + str(RA / 'overlays/borders/snes-lttp.cfg')
check('bezel-listed', pick in ids(), {})
click(pick)
shot('settings-look.png')
s.client.call('pad', press='b', hold_ms=100)
time.sleep(1.5)
shot('bezel-filter.png')
saved = s.client.call('status')['filter']
check('both', saved['preset'].endswith('crt-easymode.slangp'), saved)
s.quit()
print('ALL PASS', flush=True)
