#!/usr/bin/env python3
"""Check whole-record dialogue reading in the native build (docs/design/dialogue-typesetting.md).

Starts a new game in Chinese, takes the default protagonist and names, and reads
the story dialogue with Z, page by page.
For every record it checks that the original got one background A per original
page break (guest_stop) and one <END> (guest_confirm), never before the host page
reached it. On records with an original page break it also checks that I/K keep
the current page start, and that F7 resumes at the original page the game shows
in every language and back. Screenshots of each checked page show the name row.

Results go to dialogue-checks.json in the run directory."""
import argparse
import json
import time
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session, run_keys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--reuse-build', action='store_true')
parser.add_argument('--records', type=int, default=30, help='records to read before stopping')
args = parser.parse_args()
s = Session.launch(language='zh-Hans', reuse_build=args.reuse_build)
print('RUN', s.run, flush=True)
checks, records = [], []


def save():
    (s.run / 'dialogue-checks.json').write_text(json.dumps({'checks': checks, 'records': records},
                                                           ensure_ascii=False, indent=2) + '\n')


def check(name, passed, state):
    checks.append({'check': name, 'passed': bool(passed), 'state': state})
    save()
    print(name, 'PASS' if passed else 'FAIL', flush=True)


def status():
    return s.client.call('status')


def dialogue():
    return status().get('dialogue') or {}


def key(name, pause=.35):
    run_keys(s.client, [{'press': name, 'hold_ms': 80}])
    time.sleep(pause)


def shot(name):
    s.client.call('screenshot', path=str(s.run / f'{name}.png'))


def settle(predicate, timeout=10):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        d = dialogue()
        if predicate(d):
            return d
        time.sleep(.1)
    return dialogue()


def confirmations(event):
    rows, _ = s.events('dialogue', 0, ['guest_stop', 'guest_confirm'])
    stops = [r for r in rows if r.get('event') == event and r['kind'] == 'guest_stop']
    ends = [r for r in rows if r.get('event') == event and r['kind'] == 'guest_confirm']
    return stops, ends


def resume_point(d):
    g, stops = d.get('guest_segment', 0), d.get('stops', [])
    return stops[g - 1] if 0 < g <= len(stops) else 0


def sizes(d):
    """I then K: the page keeps its first character both times."""
    start, size = d['page_start'], d['font_size']
    key('i', .6)
    bigger = settle(lambda x: x.get('font_size') == size + 1)
    key('k', .6)
    back = settle(lambda x: x.get('font_size') == size)
    return {'start': start, 'bigger': [bigger.get('font_size'), bigger.get('page_start'), bigger.get('pages')],
            'back': [back.get('font_size'), back.get('page_start'), back.get('pages')]}, \
        bigger.get('page_start') == start and back.get('page_start') == start and back.get('event') == d['event']


def languages(d, label):
    """F7 through every language back to the first: each switch resumes where the
    original page the game shows begins, and the original is not advanced."""
    first, seen, ok = d['locale'], [], True
    for _ in range(4):
        before = dialogue()
        key('f7', .8)
        after = settle(lambda x: x.get('locale') != before.get('locale'))
        want = resume_point(after)
        seen.append({'locale': after.get('locale'), 'page_start': after.get('page_start'), 'want': want,
                     'guest': [before.get('guest_segment'), after.get('guest_segment')], 'event': after.get('event'),
                     'text': (after.get('boxes') or [{}])[0].get('text', '')[:60]})
        ok &= after.get('page_start') == want and after.get('guest_segment') == before.get('guest_segment') \
            and after.get('event') == before.get('event')
        shot(f'{label}-{after.get("locale")}')
        if after.get('locale') == first:
            break
    return seen, ok and seen[-1]['locale'] == first


# Title, then New game; the common prologue is intro screens and dialogue.
s.wait(vi=600, timeout=300)
for _ in range(20):
    key('return', .5)
    try:
        s.wait(title_major=3, timeout=6)
        break
    except Exception:
        pass
time.sleep(2)
key('return', 3)

current, switched = None, 0
sized, shots = {}, {}
end = time.monotonic() + 1200
quiet_since = None
while time.monotonic() < end and len(records) < args.records:
    st = status()
    if not s.alive():
        raise SystemExit('the host exited')
    if (st.get('name_page') or {}).get('visible'):
        # Protagonist selection and the name pages: Enter takes each default.
        quiet_since = None
        s.client.call('ui.key', key='return')
        time.sleep(1.2)
        continue
    d = st.get('dialogue') or {}
    if not d.get('active'):
        if (st.get('intro') or {}).get('loaded'):
            quiet_since = None
            key('z', .3)
            continue
        # Script scenes (maps, animations) run between the dialogue scenes.
        quiet_since = quiet_since or time.monotonic()
        if time.monotonic() - quiet_since > 90:
            break
        time.sleep(.2)
        continue
    quiet_since = None
    if d['event'] != (current or {}).get('event'):
        if current:
            stops, ends = confirmations(current['event'])
            current.update(guest_stops=len(stops), guest_confirms=len(ends))
            check(f'record-{current["event"]}-confirms', len(stops) == len(current['stops']) and len(ends) == 1, current)
        box = next((b for b in d.get('boxes', []) if b.get('active')), {})
        current = {'event': d['event'], 'text_id': box.get('text_id'), 'locale': d.get('locale'),
                   'stops': d.get('stops', []), 'pages': d.get('pages'), 'speaker': box.get('speaker'),
                   'text': box.get('text', ''), 'reached': 0}
        records.append(current)
        save()
        locale = d.get('locale')
        if shots.get(locale, 0) < 6:
            time.sleep(1.2)
            shot(f'record-{d["event"]}-{locale}-page-0')
            shots[locale] = shots.get(locale, 0) + 1
        # Read the second half in English.
        if len(records) == args.records // 2 + 1 and locale != 'en':
            for _ in range(3):
                key('f7', .8)
                if settle(lambda x: x.get('locale') == 'en', 3).get('locale') == 'en':
                    break
            current['locale'] = dialogue().get('locale')
    # Pages are only confirmed forward: the original never runs ahead of the
    # furthest host page read (a bigger font may shorten the current one).
    d = dialogue()
    if d.get('event') == current['event'] and not d.get('pending'):
        current['reached'] = max(current['reached'], sum(1 for x in d.get('stops', []) if x < d.get('page_end', 0)))
        if d.get('guest_segment', 0) > current['reached']:
            check(f'record-{current["event"]}-ahead', False, d)
    locale = d.get('locale')
    if current.get('stops') and d.get('pages', 0) >= 2 and d.get('page', 0) >= 1 and d.get('event') == current['event'] \
            and not d.get('pending') and (sized.get(locale, 0) < 2 or switched < 3) and not current.get('probed'):
        current['probed'] = True
        time.sleep(1)
        if sized.get(locale, 0) < 2:
            result, ok = sizes(d)
            check(f'record-{current["event"]}-{locale}-font-keeps-page-start', ok, result)
            sized[locale] = sized.get(locale, 0) + 1
        if switched < 3:
            result, ok = languages(dialogue(), f'record-{current["event"]}')
            check(f'record-{current["event"]}-language-resumes', ok, result)
            switched += 1
            current['reached'] = 0
    key('z', .45)

if current:
    # The last record read: wait for its <END> before counting.
    time.sleep(2)
    stops, ends = confirmations(current['event'])
    if ends or dialogue().get('event') != current['event']:
        current.update(guest_stops=len(stops), guest_confirms=len(ends))
        check(f'record-{current["event"]}-confirms', len(stops) == len(current['stops']) and len(ends) == 1, current)
save()
failed = [c['check'] for c in checks if not c['passed']]
print('RECORDS', len(records), 'CHECKS', len(checks), 'FAILED', failed, flush=True)
print('EXIT', s.quit().get('exit_code'), flush=True)
