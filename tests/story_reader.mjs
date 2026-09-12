import {test} from 'node:test';
import assert from 'node:assert/strict';
import {parseStoryHash,storyHash,lineId,selectSpeaker,sectionVisible,searchDialogue} from '../tools/data_viewer/web/story-state.js';

test('stable links preserve shared events in each scene and reject malformed anchors',()=>{
  const id=lineId('base:stage_events:0019bf10',24);
  assert.equal(id,'0019bf10-18');
  assert.deepEqual(parseStoryHash(storyHash(125,id)),{scene:125,line:id});
  assert.deepEqual(parseStoryHash('#1'),{scene:1,line:''});
  assert.deepEqual(parseStoryHash('#scene=1&line=%22onclick'),{scene:1,line:''});
  assert.equal(parseStoryHash('#scene=-3').scene,1);
});
test('relative speaker has no guessed portrait until a compatible route is selected',()=>{
  const speaker={status:'route-relative',label:'主角',candidates:[
    {key:'base:actors:0025',label:'アーク',portrait:'arc.png'},
    {key:'base:actors:0028',label:'マナミ',portrait:'manami.png'}]};
  assert.equal(selectSpeaker(speaker,'').portrait,null);
  assert.equal(selectSpeaker(speaker,'3DD4').portrait,'manami.png');
  assert.equal(selectSpeaker(speaker,'3DD2').portrait,null);
  assert.equal(speaker.status,'route-relative');
  const fixed={...speaker,status:'route-resolved-by-scene',key:'base:actors:0028',portrait:'manami.png'};
  assert.equal(selectSpeaker(fixed,'3DD1'),fixed);
});
test('route filters preserve common and choice sections without evaluating event conditions',()=>{
  assert.equal(sectionVisible('3DD6','3DD4'),true);
  assert.equal(sectionVisible('3DD5','3DD4'),false);
  assert.equal(sectionVisible('3DDA','3DD4'),true);
  assert.equal(sectionVisible(null,'3DD4'),true);
});
test('search supports names, original text, local scope and explicit result cap',()=>{
  const rows=[[0,'a',10,123,'アーク / マナミ','出撃!'],[1,'a',10,124,'ローレンス','出撃する']];
  assert.equal(searchDialogue(rows,'出撃').total,2);
  assert.equal(searchDialogue(rows,'出撃',1).rows[0][0],1);
  assert.equal(searchDialogue(rows,'マナミ').rows[0][3],123);
  assert.equal(searchDialogue(rows,'123').total,1);
  assert.equal(searchDialogue(rows,'出撃',null,1).total,2);
  assert.equal(searchDialogue(rows,'出撃',null,1).rows.length,1);
  assert.equal(searchDialogue(rows,'出').total,0);
});
