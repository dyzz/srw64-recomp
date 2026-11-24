#!/usr/bin/env python3
"""The Library's animations page (docs/native/library.md, 动画): the map movies played by
the original's own code in the host's sandbox (src/host/map_movie.hpp).

One run on the title, HD images, Chinese:
1. The Library opens on its third tab with all twelve movies listed.
2. Four movies (コン・バトラーV, ゲッタードラゴン, ゴッドマーズ, オーラロード · ズワァース)
   play: the tick runs, the game plays the movie's song and sound effects, and the stage
   shows pictures (screenshots at three points of each).
3. Pause holds the tick; a frame forward and back move it by one; the page's own Original /
   HD buttons draw that frame again in the other images.
4. Closing the Library stops the movie and gives the title its song back.
Evidence (screenshots, library-movie-checks.json) goes to the run directory."""
import json
from pathlib import Path
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

s = Session.launch(language='zh-Hans', images='hd')
print('RUN', s.run, flush=True)
checks = []
SONG_PLAYING = 0x800FFA6C


def status():
    return s.client.call('status')


def movie():
    return status().get('map_movie') or {}


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run / 'library-movie-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    print(name, 'PASS' if passed else 'FAIL', json.dumps(state, ensure_ascii=False)[:300], flush=True)


def shot(name):
    s.client.call('screenshot', path=str(s.run / name))


def keys(*names, pause=.4):
    for name in names:
        run_keys(s.client, [{'press': name}])
        time.sleep(pause)


def ids():
    found = {}
    def walk(node):
        if isinstance(node, dict):
            if node.get('id'):
                found[node['id']] = node.get('enabled', True)
            for value in node.values():
                walk(value)
        elif isinstance(node, list):
            for value in node:
                walk(value)
    walk(s.client.call('ui.tree'))
    return found


def song():
    return int.from_bytes(bytes.fromhex(s.client.call('memory.read', address=SONG_PLAYING, size=2)['hex']), 'big', signed=True)


def to_ring():
    for _ in range(30):
        if (status().get('intro') or {}).get('title_major') == 3:
            time.sleep(3)
            return
        keys('return', pause=.5)
    raise AssertionError('title ring not reached')


try:
    s.wait(vi=600, timeout=120)
    to_ring()
    title_song = song()
    s.client.call('ui.click', id='library-open')
    time.sleep(1.5)
    s.client.call('ui.click', id='lib-tab:2')
    time.sleep(1.5)
    shot('library-movies.png')
    found = ids()
    check('twelve-movies', all(f'lib-item:{i}' in found for i in range(12)) and 'mv-stage' in found and 'mv-play' in found,
          sorted(k for k in found if k.startswith(('lib-', 'mv-'))))

    def start_row(row, still=None):
        """Chooses a row and makes sure it plays (a click in pointer mode already starts it)."""
        s.client.call('ui.click', id=f'lib-item:{row}')
        time.sleep(1.0)
        if still:
            shot(still)
        if not movie().get('playing'):
            s.client.call('ui.click', id='mv-play')
            time.sleep(.8)

    # The list's order is by work; find each movie's row by the play it starts.
    def play_row(row, label, seconds):
        start_row(row, f'{label}-still.png')
        state = movie()
        times = []
        for k in range(3):
            time.sleep(seconds / 4)
            m = movie()
            times.append(m.get('tick'))
            shot(f'{label}-{k}.png')
        state = movie()
        check(f'{label}-plays', state.get('movie', -1) >= 0 and len(set(times)) == 3 and times == sorted(times) and state.get('songs'),
              {'movie': state.get('movie'), 'ticks': times, 'songs': state.get('songs'), 'sounds': state.get('sounds', [])[:12]})
        return state

    rows = {}
    for row in range(12):
        start_row(row)
        rows[movie().get('movie')] = row
    check('rows-map-to-movies', sorted(k for k in rows if k is not None) == list(range(12)), rows)

    played = {}
    for number, label, seconds in ((0, 'combattler', 16), (5, 'getter-dragon', 8), (11, 'godmars', 18), (1, 'aura-zwarth', 11)):
        played[label] = play_row(rows[number], label, seconds)
    # The last one runs to its end.
    time.sleep(4)
    end = movie()
    check('aura-zwarth-ends', not end.get('playing') and end.get('tick', 0) >= 320, end)

    # Pause, a frame on and back.
    start_row(rows[0])
    time.sleep(5)
    s.client.call('ui.click', id='mv-play')
    time.sleep(1)
    a = movie()['tick']
    time.sleep(1.5)
    b = movie()['tick']
    s.client.call('ui.click', id='mv-fwd')
    time.sleep(.8)
    c = movie()['tick']
    s.client.call('ui.click', id='mv-back')
    s.client.call('ui.click', id='mv-back')
    time.sleep(.8)
    d = movie()['tick']
    shot('paused.png')
    check('pause-and-step', a == b and c == b + 1 and d == c - 2, {'paused': [a, b], 'forward': c, 'back2': d})

    # The page's own Original / HD: the same paused frame in both.
    found = ids()
    s.client.call('ui.click', id='mv-img:0')
    time.sleep(1.2)
    shot('paused-original.png')
    s.client.call('ui.click', id='mv-img:1')
    time.sleep(1.2)
    shot('paused-hd.png')
    from PIL import Image, ImageChops
    stage = (640, 180, 1800, 1200)   # inside the stage on a 2560 x 1600 window
    original = Image.open(s.run / 'paused-original.png').convert('RGB').crop(stage)
    hd = Image.open(s.run / 'paused-hd.png').convert('RGB').crop(stage)
    difference = ImageChops.difference(original, hd).convert('L')
    changed = sum(1 for v in difference.getdata() if v > 24) / (difference.width * difference.height)
    check('page-image-toggle', 'mv-img:0' in found and 'mv-img:1' in found and changed > .2 and movie()['tick'] == d,
          {'changed': round(changed, 3), 'tick': movie()['tick']})

    # Closing gives the title its song back.
    s.client.call('ui.click', id='settings-close')
    time.sleep(2)
    after = movie()
    check('close-stops', not after.get('playing') and song() == title_song, {'movie': after, 'song': song(), 'title_song': title_song})
    shot('closed.png')
finally:
    print(json.dumps([{c['check']: c['passed']} for c in checks], ensure_ascii=False))
    s.quit()
