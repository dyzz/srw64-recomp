#!/usr/bin/env python3
"""MOD music in a running game (docs/design/audio-mods.md §8–§9): a package dragged into the
window, the sound test, the settings, and a mini stage's battle songs and jingle.

The package is one the website's BGM maker exports (web/src/pages/[lang]/mods/bgm.astro) with
  songs 7 and 23 replaced, 7 titled 测试主题, 38 a short jingle that plays once and 44 (map
  20's stage song) a steady 300 Hz tone; music.json battle.pilot.4 = 23 (ドモン fights to 23).

    tools/recomp/debug/check_mod_music.py PACKAGE.srw64mod --part title
    tools/recomp/debug/check_mod_music.py PACKAGE.srw64mod --part stage

title: install by drop; the sound test marks the replaced songs and shows the package's
  title; a replaced song is what plays (its tone in the recording); MOD music volume 50 %
  halves it; turned off, the original plays again.
stage: config/recomp/mini-stages/mod-music.json. レイン's scripted duel keeps the original
  song 4; ドモン's plays 23 from the package; the opening's jingle plays once and map 20's song
  44 comes back from the package.
Evidence (checks, screenshots, recordings, the song timeline) goes to the run directory."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

ROOT = Path(__file__).resolve().parents[3]
CURRENT_SONG = 0x800FFA6C   # D_800FFA6C (8007E810)
parser = argparse.ArgumentParser()
parser.add_argument('package')
parser.add_argument('--part', choices=('title', 'stage'), required=True)
args = parser.parse_args()
package = Path(args.package).resolve()

if args.part == 'title':
    # campaigns: the MOD entry is shown (SRW64_MOD_ENTRY), and with it the MOD music settings.
    s = Session.launch(language='zh-Hans', images='original', audio=True, campaigns=True)
else:
    s = Session.launch(language='zh-Hans', images='original', audio=True, mini_stage=str(ROOT / 'config/recomp/mini-stages/mod-music.json'))
print('RUN', s.run, flush=True)
checks = []
settings_file = s.run / 'presentation-settings.json'
command = 'cmd' if sys.platform == 'darwin' else 'control'


def status():
    return s.client.call('status')


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    (s.run / f'mod-music-{args.part}-checks.json').write_text(json.dumps(checks, ensure_ascii=False, indent=2) + '\n')
    print(name, 'PASS' if passed else 'FAIL', json.dumps(state, ensure_ascii=False)[:300], flush=True)
    assert passed, name


def wait(predicate, what, timeout=60):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        st = status()
        if predicate(st):
            return st
        time.sleep(.2)
    raise AssertionError(f'timed out: {what}')


def current_song():
    return int(s.client.call('memory.read', address=CURRENT_SONG, size=2)['hex'], 16)


def key(name, pause=.35):
    run_keys(s.client, [{'press': name, 'hold_ms': 90}])
    time.sleep(pause)


def ui(name, *modifiers, pause=.35):
    s.client.call('ui.key', key=name, modifiers=list(modifiers))
    time.sleep(pause)


def click(control, pause=.4):
    s.client.call('ui.click', id=control)
    time.sleep(pause)


def install():
    s.client.call('ui.drop', path=str(package))
    manifest = s.run.parent / (s.run.name + '.mods') / 'test.bgm' / 'mod.json'
    end = time.monotonic() + 60
    while not manifest.exists() and time.monotonic() < end:
        time.sleep(.3)
    check('installed', manifest.exists(), {'manifest': str(manifest)})
    # A finished install opens the MOD manager, inside the settings window: close it.
    time.sleep(1.5)
    if status()['ui']['input_owners'].get('settings'):
        s.client.call('screenshot', path=str(s.run / 'installed.png'))
        click('settings-close', pause=1.0)
    wait(lambda st: not st['ui']['input_owners'].get('settings'), 'the MOD manager closes', 20)


def tone(seconds, name):
    """Record `seconds` and return the audio's RMS and strongest frequency."""
    import numpy as np
    video = s.run / f'{name}.mp4'
    s.record(seconds, video)
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', str(video), '-vn', '-ac', '1', '-ar', '44100', '-f', 's16le', '-'],
                         capture_output=True, check=True).stdout
    x = np.frombuffer(raw, dtype='<i2').astype(float)[44100 // 2:]   # skip the start of the capture
    spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    peak = float(np.fft.rfftfreq(len(x), 1 / 44100)[int(np.argmax(spectrum[20:])) + 20])
    return {'rms': round(float(np.sqrt(np.mean(x * x))), 1), 'peak_hz': round(peak, 1)}


def ring_idle(st):
    step = (st.get('intro') or {}).get('step') or {}
    return step.get('major') == 3 and step.get('substate') == 2


def page(screen, timeout=60, **fields):
    return wait(lambda st: st['title_page'].get('visible') and st['title_page'].get('screen') == screen
                and all(st['title_page'].get(k) == v for k, v in fields.items()), f'title page {screen} {fields}', timeout)['title_page']


def settings(*controls):
    """Open the settings window on its general page, press the controls, close it."""
    ui(',', command, pause=1.0)
    wait(lambda st: st['ui']['input_owners']['settings'], 'the settings window opens', 20)
    click('settings-page:general', pause=.8)
    for control in controls:
        click(control, pause=.8)
    click('settings-close', pause=1.0)
    return json.loads(settings_file.read_text()) if settings_file.exists() else {}


if args.part == 'title':
    s.wait(vi=600, timeout=300)
    install()
    for _ in range(30):
        if ring_idle(status()):
            break
        key('return', pause=.6)
    wait(ring_idle, 'ring idle', 90)
    time.sleep(1.5)
    key('right', pause=.2)            # オプション
    wait(ring_idle, 'ring idle')
    time.sleep(.3)
    key('return')
    page('options')
    time.sleep(1.0)
    for _ in range(6):                # select 音乐鉴赏, then open it; a click right as the page appears is lost
        click('tp-option:1', pause=.8)
        if status()['title_page'].get('screen') == 'sound':
            break
    p = page('sound')
    s.client.call('screenshot', path=str(s.run / 'sound-list.png'))
    rows = {row['song']: row for row in p['songs']}
    replaced = sorted(song for song, row in rows.items() if row.get('replaced'))
    check('sound-list-marks', replaced == [7, 23, 38, 44] and rows[7]['text'] == '测试主题',
          {'replaced': replaced, '7': rows[7]['text']})
    index = {row['song']: i for i, row in enumerate(p['songs'])}

    def play(song):
        target = index[song]
        while (cur := status()['title_page']['current']) != target:
            ui('down' if cur < target else 'up', pause=.12)
        ui('return', pause=1.5)
        return status()

    st = play(44)
    full = tone(4, 'song-44-volume-100')
    check('replaced-song-plays', st['music'].get('replaced') and st['music'].get('song') == 44 and current_song() == 44
          and abs(full['peak_hz'] - 300) < 5, {'music': st['music'], 'tone': full})
    saved = settings('mod-music-volume:50')
    check('volume-saved', saved.get('mod_music_volume') == 50 and saved.get('mod_music') is True, {k: saved.get(k) for k in ('mod_music', 'mod_music_volume')})
    play(7)
    play(44)
    half = tone(4, 'song-44-volume-50')
    ratio = half['rms'] / max(full['rms'], 1)
    check('volume-halves', 0.4 < ratio < 0.6 and abs(half['peak_hz'] - 300) < 5, {'full': full, 'half': half, 'ratio': round(ratio, 3)})
    saved = settings('mod-music-volume:100', 'mod-music:off')
    st = play(7)
    st = play(44)
    off = tone(4, 'song-44-mod-off')
    check('off-plays-original', saved.get('mod_music') is False and not st['music'].get('replaced') and current_song() == 44
          and abs(off['peak_hz'] - 300) > 5, {'music': st['music'], 'tone': off, 'saved': saved.get('mod_music')})
    saved = settings('mod-music:on')
    check('on-again', saved.get('mod_music') is True and saved.get('mod_music_volume') == 100, {k: saved.get(k) for k in ('mod_music', 'mod_music_volume')})
    ui('escape', pause=.8)

else:
    s.wait(vi=600, timeout=300)
    install()
    # The song timeline, watched from before entering: entry returns only after the opening,
    # which holds the duels and the jingle. Its own connection, beside the main thread's.
    import threading
    from recomp.debug.session import Client
    timeline, stop_watch = [], threading.Event()

    def watch():
        c = Client(s.run)
        while not stop_watch.is_set():
            try:
                st = c.call('status')
                song = int(c.call('memory.read', address=CURRENT_SONG, size=2)['hex'], 16)
            except Exception:
                time.sleep(.2)
                continue
            entry = {'t': round(time.monotonic(), 2), 'vi': st.get('vi'), 'song': song, 'replaced': bool(st['music'].get('replaced')),
                     'music_song': st['music'].get('song')}
            if not timeline or (entry['song'], entry['replaced']) != (timeline[-1]['song'], timeline[-1]['replaced']):
                timeline.append(entry)
                print('SONG', entry, flush=True)
                (s.run / 'mod-music-timeline.json').write_text(json.dumps(timeline, indent=1) + '\n')
            time.sleep(.1)
    watcher = threading.Thread(target=watch, daemon=True)
    watcher.start()
    s.enter_mini_stage()
    end = time.monotonic() + 120
    seen_jingle_end = False
    while time.monotonic() < end:
        if any(e['song'] == 38 for e in timeline) and timeline[-1]['song'] == 44 and timeline[-1]['replaced']:
            seen_jingle_end = True
            break
        time.sleep(.2)
    stop_watch.set()
    watcher.join(5)
    songs = [(e['song'], bool(e['replaced'])) for e in timeline]
    check('rain-keeps-original', (4, False) in songs, {'songs': songs})
    check('domon-plays-package-song', (23, True) in songs and songs.index((23, True)) > songs.index((4, False)), {'songs': songs})
    jingle = [e for e in timeline if e['song'] == 38]
    back = [e for e in timeline if e['song'] == 44 and e['replaced'] and jingle and e['t'] > jingle[0]['t']]
    check('jingle-once-then-stage', bool(jingle) and jingle[0]['replaced'] and seen_jingle_end and back
          and 3.0 < back[0]['t'] - jingle[0]['t'] < 10.0,
          {'jingle': jingle[:1], 'back': back[:1], 'seconds': round(back[0]['t'] - jingle[0]['t'], 2) if jingle and back else None})
    stage = tone(4, 'stage-44-after-jingle')
    check('stage-song-audible', abs(stage['peak_hz'] - 300) < 5, stage)

print('ALL PASS', args.part, flush=True)
