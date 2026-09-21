#!/usr/bin/env python3
"""Exercise the current native build via title-menu mini-stage entry through the host debug socket.

Writes ui-checks.json and GPU screenshots into that isolated run directory.
Use after launching the battle-ui mini stage; leaves the next encounter open.
"""
import sys,time,json
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from recomp.debug.session import Session
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('run', type=Path, nargs='?', help='Attach an existing run; omit to build and launch native UI')
args=parser.parse_args()
if args.run:
 s=Session(args.run.resolve())
else:
 s=Session.launch(language='zh-Hans',mini_stage='config/recomp/mini-stages/battle-ui.json')
 print('RUN',s.run,flush=True)
 s.enter_mini_stage()
 # Empty map tile -> original phase menu -> end player phase. Each input goes
 # through the debug keyboard endpoint; no boot/name-grid frame recording.
 for key in ['up','z','z','z']:
  s.client.call('keys',press=key,hold_ms=100);time.sleep(.35)
run=s.run;checks=[]
def state():return s.client.call('status')
def wait(fn, seconds=120):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  st=state()
  if fn(st):return st
  time.sleep(.1)
 raise AssertionError('state wait timeout')
def check(name, cond, st):
 checks.append({'check':name,'passed':bool(cond),'vi':st['vi'],'battle':st['battle_page']})
 (run/'ui-checks.json').write_text(json.dumps(checks,ensure_ascii=False,indent=2))
 assert cond,name
 print(name,'PASS',flush=True)
def click(action):
 s.client.call('ui.click',id='battle-'+action);time.sleep(.3);return state()
def shot(name):s.client.call('screenshot',path=str(run/(name+'.png')))
st=wait(lambda x:x['battle_page'].get('visible',False));base=st['battle_page'];check('enemy-confirm-visible',base['mode']==2,st);shot('battle-preview')
check('art-and-defense-both-sides',all(base[k].get('unit_art') and base[k].get('portrait') and set(base[k]['defense'])>={'parry','shield','clone','incoming_cuttable'} for k in ['attacker','defender']),st)
check('aura-weapon-eligibility',base['attacker']['barrier']['kind']=='aura' and base['attacker']['barrier']['status']=='non_beam' and base['attacker']['damage']==base['attacker']['damage_raw'],st)
st=click('animation');check('animation-toggle',st['battle_page']['animation']!=base['animation'],st)
st=click('evade');check('evade-recalculates',st['battle_page']['response']==1 and st['battle_page']['attacker']['hit']==base['attacker']['hit']//2 and st['battle_page']['defender']['weapon']==-1,st);shot('battle-evade')
st=click('defend');check('defend-disables-counter',st['battle_page']['response']==2 and st['battle_page']['defender']['weapon']==-1,st)
st=click('weapon');check('original-weapon-picker',not st['battle_page']['visible'],st);shot('weapon-picker')
s.client.call('keys',press='z',hold_ms=100)
st=wait(lambda x:x['battle_page'].get('visible',False),10);check('weapon-selection-returns',st['battle_page']['response']==0 and st['battle_page']['defender']['weapon']>=0,st)
for lang,w,h in [('en',800,600),('ja',1100,760),('zh-Hans',960,720)]:
 s.client.call('settings',locale=lang);s.client.call('window',width=w,height=h);time.sleep(.4)
 st=state();check('locale-size-'+lang,st['locale']==lang and st['window']['width']==w and st['battle_page']['visible'],st);shot('battle-'+lang)
st=click('confirm');check('confirm-leaves-modal',not st['battle_page']['visible'],st)
st=wait(lambda x:x['battle_page'].get('visible',False),40);check('next-encounter',st['battle_page']['serial']>base['serial'],st)
shot('next-encounter')
print('DONE',flush=True)
