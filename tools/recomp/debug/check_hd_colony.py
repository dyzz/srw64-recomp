#!/usr/bin/env python3
"""Capture the original and HD colony at all eight game-driven phases, then scroll.

This is a native-host smoke check, not an emulator or every-map acceptance test.
The frame events on both sides of each screenshot must agree before it is labelled.
"""
import argparse
import json
import time
from pathlib import Path

from tools.recomp.debug.session import Session

ROOT=Path(__file__).resolve().parents[3]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,required=True)
    args=parser.parse_args()
    session=Session.launch(language='zh-Hans',images='hd',binary=str(ROOT/'build/recomp/gfx-build/srw64-gfx-host'),
        mini_stage=str(ROOT/'config/recomp/mini-stages/map87-colony-view.json'),
        env={'SRW64_HD_MAPS':str(args.runtime.resolve())})
    print('RUN',session.run,flush=True)
    rows=[]

    def latest():
        path=session.run/'hd-colony-events.jsonl'
        lines=path.read_text().splitlines() if path.exists() else []
        return json.loads(lines[-1]) if lines else None

    def shot(name):
        before=latest()
        session.client.call('screenshot',path=str(session.run/name))
        after=latest()
        row={'file':name,'observed_before':before,'observed_after':after}
        rows.append(row)
        return before==after

    try:
        session.enter_mini_stage();time.sleep(1)
        for mode in ('hd','original'):
            if mode=='original':
                session.client.call('keys',press='f6',hold_ms=80);time.sleep(.6)
            seen=set();deadline=time.monotonic()+20
            while len(seen)<8 and time.monotonic()<deadline:
                event=latest()
                if not event or event['hd']!=(mode=='hd') or event['frame'] in seen:
                    time.sleep(.03);continue
                # Give the renderer time to consume the counter's display list.
                time.sleep(.08)
                event=latest();frame=event['frame']
                if shot(f'{mode}-colony-frame-{frame}.png'):
                    seen.add(frame);print('CAPTURE',mode,frame,flush=True)
            if len(seen)!=8:
                raise RuntimeError(f'{mode}: incomplete phases {sorted(seen)}')
        session.client.call('keys',press='f6',hold_ms=80);time.sleep(.5)
        session.client.call('keys',down='right')
        for i in range(4):
            time.sleep(.5);shot(f'hd-colony-scroll-{i}.png')
        session.client.call('keys',release_all=True);time.sleep(.5)
        shot('hd-colony-scrolled.png')
        session.client.call('keys',press='f6',hold_ms=80);time.sleep(.5)
        shot('original-colony-scrolled.png')
        session.client.call('keys',press='f6',hold_ms=80);time.sleep(.5)
        shot('hd-colony-restored.png')
    finally:
        (session.run/'colony-captures.json').write_text(json.dumps(rows,indent=2)+'\n')
        session.quit()
        summary=session.run/'hd-map-summary.json'
        print('SUMMARY',summary.read_text() if summary.exists() else 'missing',flush=True)


if __name__=='__main__':
    main()
