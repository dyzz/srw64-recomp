#!/usr/bin/env python3
"""Capture each selected HD map and original at three views in the native host.

Uses the existing host binary and refuses to overwrite evidence folders. Each
record binds the captured images and logs to the exact runtime asset hashes.
"""
import argparse
import json
import shutil
import time
from pathlib import Path

from tools.hd_ai.tactical_kit_compose import sha
from tools.recomp.debug.session import Session


def set_image_mode(session, mode):
    """Request an explicit mode and observe it before capturing.

    F6 is a relative toggle; queued/repeated key events can undo it between
    views. The debug settings command makes capture mode deterministic.
    """
    session.client.call('keys',release_all=True)
    session.client.call('settings',images=mode)
    expected=1 if mode=='hd' else 0
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        state=session.client.call('status')
        if state['image_mode']['current']==expected and state['image_mode']['requested']==bool(expected):
            time.sleep(.2)
            return
        time.sleep(.1)
    raise RuntimeError(f'Image mode did not settle to {mode}')


def main():
    ROOT=Path(__file__).resolve().parents[3]
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--maps',type=int,nargs='+',required=True)
    args=parser.parse_args()
    runtime=args.runtime.resolve()
    binary=ROOT/'build/recomp/gfx-build/srw64-gfx-host'
    template=json.loads((ROOT/'config/recomp/mini-stages/map87-colony-view.json').read_text())
    for number in args.maps:
        folder=runtime/f'map-{number:03d}'
        evidence=folder/'runtime-evidence'
        evidence.mkdir(exist_ok=False)
        meta=json.loads((folder/'meta.json').read_text())
        stage=json.loads(json.dumps(template))
        stage.update(name=f'map{number}-registration-review',map=number,
                     note='Inspect the registered tactical-map candidate at center and after diagonal scrolling.')
        stage['events'][0]['commands'][3]['args']=[meta['width']//32,meta['height']//32]
        stagepath=evidence/'stage.json'
        stagepath.write_text(json.dumps(stage,indent=2)+'\n')
        session=Session.launch(language='zh-Hans',images='hd',binary=str(binary),mini_stage=str(stagepath),env={'SRW64_HD_MAPS':str(runtime)})
        print('RUN',number,session.run,flush=True)
        captures=[]
        try:
            session.enter_mini_stage()
            time.sleep(1)
            for label,direction in [('center',None),('scrolled-up-left','up+left'),('scrolled-down-right','down+right')]:
                if direction:
                    session.client.call('keys',down=direction)
                    time.sleep(6)
                    session.client.call('keys',release_all=True)
                    time.sleep(.6)
                for mode in ['hd','original']:
                    set_image_mode(session,mode)
                    state=session.client.call('status')
                    assert state['image_mode']['current']==(1 if mode=='hd' else 0),state['image_mode']
                    name=f'{label}-{mode}.png'
                    session.client.call('screenshot',path=str(evidence/name))
                    after=session.client.call('status')
                    assert after['image_mode']['current']==state['image_mode']['current'],after['image_mode']
                    captures.append({'file':name,'state':state})
                set_image_mode(session,'hd')
        finally:
            (evidence/'captures.json').write_text(json.dumps(captures,indent=2)+'\n')
            session.quit()
        for name in ['hd-map-summary.json','image-mode-events.jsonl','hd-colony-events.jsonl']:
            path=session.run/name
            if path.exists():shutil.copy2(path,evidence/name)
        summary=json.loads((evidence/'hd-map-summary.json').read_text())
        assert summary['native_draws']>0 and summary['rewritten_draws']>0 and summary['skipped']==0,summary
        record={'schema':'srw64.tactical-map-runtime-smoke.v1','map':number,'run':str(session.run),
                'base_sha256':sha(folder/'base.png'),'index_sha256':sha(folder/'index.png'),
                'meta_sha256':sha(folder/'meta.json'),'host_binary_sha256':sha(binary),
                'capture_code_sha256':sha(Path(__file__)),
                'mode_control':'Explicit debug settings request with observed mode before and after capture',
                'native_host_smoke_verified':True,'full_acceptance_proven':False,
                'observed':['HD and original capture pairs at center and after two diagonal scrolls',
                            'Native HD draws recorded without skipped draws','HD/original/HD mode switches'],
                'not_proven':['All terrain cells visually accepted','Exact camera coordinate at each capture',
                              'Performance and palette-cycle timing equivalence'],
                'files':{p.name:sha(p) for p in evidence.iterdir() if p.is_file()}}
        (evidence/'evidence.json').write_text(json.dumps(record,indent=2)+'\n')
        print('VERIFIED',number,summary,flush=True)


if __name__=='__main__':
    main()
