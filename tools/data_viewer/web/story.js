import {sectionVisible, lineId, storyHash, parseStoryHash, selectSpeaker, searchDialogue} from './story-state.js';
const $ = id => document.getElementById(id);
const cache = new Map(), phaseNames = {all:'整章',opening:'开场',deployment:'初期配置',map:'战场事件',ending:'结束'};
let index, current, phase = 'all', revision = 0, searchRevision = 0, searchTimer, selectedLine = '';
const pad4 = n => String(n).padStart(4, '0');
function element(tag, text, cls) {const e=document.createElement(tag);if(text!==undefined)e.textContent=text;if(cls)e.className=cls;return e;}
function catalogLink(key, text) {const a=element('a',text);a.href=`index.html#${encodeURIComponent(key)}`;return a;}
function fail(error) {$('error').hidden=false;$('error').textContent='无法读取剧情数据：'+error.message;}
async function get(path) {
  if(!cache.has(path)) cache.set(path,fetch(path).then(r=>{if(!r.ok)throw Error(path+' HTTP '+r.status);return r.json();}).catch(e=>{cache.delete(path);throw e;}));
  return cache.get(path);
}
function savePreferences() {
  try {localStorage.setItem('srw64.story.reader.v1',JSON.stringify(Object.fromEntries(['route','show-portraits','show-structure','show-source','font-size'].map(id=>[id,$(id).type==='checkbox'?$(id).checked:$(id).value]))));}catch{}
}
function loadPreferences() {
  try {const p=JSON.parse(localStorage.getItem('srw64.story.reader.v1')||'{}');for(const [id,v] of Object.entries(p)){const e=$(id);if(!['route','show-portraits','show-structure','show-source','font-size'].includes(id))continue;if(e.type==='checkbox'&&typeof v==='boolean')e.checked=v;else if([...e.options].some(o=>o.value===v))e.value=v;}}catch{}
}
function highlighted(text, query) {
  const f=document.createDocumentFragment(), q=query.trim().toLocaleLowerCase();
  if(q.length<2){f.append(document.createTextNode(text));return f;}
  const lower=text.toLocaleLowerCase();let start=0,pos;
  while((pos=lower.indexOf(q,start))>=0){f.append(document.createTextNode(text.slice(start,pos)),element('mark',text.slice(pos,pos+q.length)));start=pos+q.length;}
  f.append(document.createTextNode(text.slice(start)));return f;
}
function renderText(display) {
  const box=element('div',undefined,'line-text');
  display.split('<STOP>').forEach((page,i)=>{if(i){const br=element('span','▸','page-break');br.title='原文翻页';box.append(br);}page.split('<BR>').forEach((part,j)=>{if(j)box.append(document.createElement('br'));box.append(highlighted(part,$('dialogue-search').value));});});
  return box;
}
function renderLine(line,event) {
  if(line.kind==='dialogue') {
    const speaker=selectSpeaker(line.speaker,$('route').value), card=element('div',undefined,'line dialogue');
    card.id='line-'+lineId(event.key,line.offset);card.tabIndex=-1;card.dataset.speaker=speaker.key||speaker.label;
    if(lineId(event.key,line.offset)===selectedLine)card.classList.add('linked-line');
    if($('show-structure').checked)card.style.marginLeft=`${Math.min(line.depth,5)*10}px`;
    const side=element('div',undefined,'line-side');
    if(speaker.portrait){const img=element('img');img.src=speaker.portrait;img.alt=speaker.label+'头像';img.width=48;img.height=48;img.loading='lazy';side.append(img);}else{side.append(element('span',speaker.status==='route-relative'?'?':'—','no-portrait'));side.title=speaker.status==='route-relative'?'需要选择主角路线后确定头像':'暂无头像';}
    const body=element('div',undefined,'line-body'), who=element('div',undefined,'line-speaker');
    who.append(speaker.key?catalogLink(speaker.key,speaker.label):element('span',speaker.label));
    if(speaker.status==='route-relative')who.append(element('small','候选：'+(speaker.candidates||[]).map(c=>c.label).join(' / ')));
    const locate=element('a','定位','line-anchor');locate.href=storyHash(current.scene,lineId(event.key,line.offset));locate.title=`定位文本 ${line.text_id}，可复制此链接`;who.append(locate);
    const meta=element('div',undefined,'line-meta');meta.append(catalogLink('base:t00_'+String(line.text_id).padStart(5,'0'),'文本 '+line.text_id),catalogLink(event.key,'事件 '+event.key.split(':').pop()),element('span','显示模式 '+line.mode));
    body.append(who,renderText(line.display),meta);card.append(side,body);return card;
  }
  if(line.kind==='section')return element('div',line.label,'line section');
  if(line.kind==='choice'){const card=element('div',undefined,'line choice');card.append(element('span','选择肢','chip'));for(const option of line.options)card.append(element('span',option.display,'option'));return card;}
  if(!$('show-structure').checked)return null;
  const labels={condition:'若 · ',statement:'',note:'', 'block-end':'条件块结束'};
  const d=element('div',(labels[line.kind]||'')+(line.text||''),'line '+line.kind);d.style.paddingLeft=`${16+line.depth*10}px`;return d;
}
function renderScene() {
  const doc=current,article=$('story');article.replaceChildren();
  document.body.classList.toggle('no-portraits',!$('show-portraits').checked);document.body.classList.toggle('no-source',!$('show-source').checked);document.body.classList.toggle('structure-hidden',!$('show-structure').checked);document.documentElement.style.setProperty('--reading-size',$('font-size').value+'px');
  const head=element('div',undefined,'scene-head'), titles=element('div'),count=element('div',undefined,'scene-count');
  head.append(element('div',pad4(doc.scene).slice(1),'scene-number'));titles.append(element('h2',doc.title||'场景 '+doc.scene));
  const flow=element('div',undefined,'scene-flow');
  for(const [label,scenes] of [['来自',doc.previous_scenes],['流向',doc.next_scenes]])for(const s of scenes){const a=element('a',`${label}：${s.scene} ${s.title||''}`);a.href=storyHash(s.scene);flow.append(a);}
  if(doc.protagonist)flow.append(element('span','本话主角：'+doc.protagonist.label));
  if(doc.shared_scene_indices.length)flow.append(element('span','共用脚本：'+doc.shared_scene_indices.join(' / ')));
  const links=element('div',undefined,'scene-links');links.append(catalogLink('base:scenarios:'+pad4(doc.scene),'关卡数据与完整指令 ↗'),catalogLink(doc.map_key,'查看本章地图 ↗'));titles.append(flow,links);head.append(titles,count);article.append(head);
  const phases=$('phase-nav');phases.replaceChildren();
  for(const [id,name] of Object.entries(phaseNames)){
    const n=doc.events.filter(e=>id==='all'||e.phase===id).reduce((a,e)=>a+e.dialogue_count,0);const b=element('button',`${name} ${n}`);b.setAttribute('aria-pressed',String(phase===id));b.onclick=()=>{phase=id;selectedLine='';renderScene();};phases.append(b);
  }
  const jump=$('event-jump');jump.replaceChildren(element('option','跳转本章事件…'));jump.firstChild.value='';
  let visible=0,lastSpeaker=null,tone=false;
  doc.events.forEach((event,n)=>{
    const option=element('option',`${String(n+1).padStart(2,'0')} ${event.phase_label} · ${event.trigger}`);option.value=event.key;jump.append(option);
    if(phase!=='all'&&phase!==event.phase)return;
    const block=element('section',undefined,'event phase-'+event.phase);block.id='event-'+event.key.split(':').pop();
    const title=element('div',undefined,'event-title');title.append(element('span',`${event.phase_label} ${String(n+1).padStart(2,'0')}`,'phase-chip'),element('strong',event.trigger),element('small',`${event.dialogue_count} 句`));block.append(title);
    let hidden=0,rendered=0;
    for(const line of event.lines){
      if(!sectionVisible(line.kind==='section'?line.marker:line.section,$('route').value)){hidden++;continue;}
      const node=renderLine(line,event);if(!node)continue;
      if(line.kind==='dialogue'){visible++;if(lastSpeaker!==node.dataset.speaker){tone=!tone;lastSpeaker=node.dataset.speaker;}node.classList.toggle('speaker-orange',!tone);}
      block.append(node);rendered++;
    }
    if(hidden)block.append(element('div',`已按主角路线隐藏 ${hidden} 条内容；条件与选择分支未求值。`,'line filtered'));
    if(!rendered&&!hidden)block.append(element('p',event.commands_omitted?'此事件主要为演出或配置，打开“条件与演出提示”或查看完整指令。':'此事件没有对白。','empty'));
    article.append(block);
  });
  count.textContent=`当前 ${visible} / ${doc.counts.dialogue} 句`;
  const position=index.scenes.findIndex(s=>s.scene===doc.scene);$('chapter-prev').disabled=position<=0;$('chapter-next').disabled=position>=index.scenes.length-1;
  for(const [id,step] of [['chapter-prev',-1],['chapter-next',1]]){$(id).title='按场景索引浏览；剧情流向见章节标题下方';$(id).onclick=()=>{const s=index.scenes[position+step];if(s)location.hash=storyHash(s.scene);};}
}
function renderList() {
  const q=$('scene-search').value.trim().toLocaleLowerCase();let count=0;const list=$('scene-list');list.replaceChildren();
  for(const s of index.scenes){if(q&&!`${s.scene} ${s.title||''}`.toLocaleLowerCase().includes(q))continue;count++;const li=element('li'),a=element('a');a.href=storyHash(s.scene);a.append(element('span',String(s.scene).padStart(3,'0'),'scene-index'),element('span',s.title||'（无标题）','scene-title'),element('small',`${s.counts.dialogue} 句 · ${s.counts.events} 个事件`));if(current?.scene===s.scene)a.setAttribute('aria-current','true');li.append(a);list.append(li);}
  $('chapter-count').textContent=`${count} / ${index.scenes.length}`;if(!count)list.append(element('li','没有匹配章节','empty'));
}
async function search() {
  const token=++searchRevision,q=$('dialogue-search').value.trim();
  if(q.length<2){$('search-results').hidden=true;return;}
  $('search-results').hidden=false;$('search-status').textContent='正在搜索原文…';$('search-hits').replaceChildren();
  try{
    const rows=await get('story/search.json');if(token!==searchRevision)return;
    const result=searchDialogue(rows,q,$('search-scope').value==='scene'?current?.scene:null);
    $('search-status').textContent=`找到 ${result.total} 句${result.total>200?' · 显示前 200 句，请缩小关键词':''} · 点击定位，定位时会显示该句所在的路线段`;
    const hits=$('search-hits');for(const r of result.rows){const a=element('a',undefined,'search-hit'),scene=index.scenes.find(s=>s.scene===r[0]);a.href=storyHash(r[0],lineId(r[1],r[2]));a.append(element('small',`${String(r[0]).padStart(3,'0')} ${scene?.title||''} · 文本 ${r[3]}`));const who=element('strong');who.append(highlighted(r[4],q));const p=element('p');const plain=r[5].replaceAll('<BR>',' ').replaceAll('<STOP>',' ▸ ');const at=plain.toLocaleLowerCase().indexOf(q.toLocaleLowerCase());const start=Math.max(0,at-60),excerpt=plain.slice(start,start+190);p.append(highlighted((start?'…':'')+excerpt+(start+190<plain.length?'…':''),q));a.append(who,p);hits.append(a);}
    if(!result.total)hits.append(element('p','没有匹配对白。可尝试人物日文名或更短的关键词。','empty'));
  }catch(e){if(token===searchRevision)$('search-status').textContent='搜索失败：'+e.message;}
}
async function route() {
  const token=++revision;
  try{
    const target=parseStoryHash(location.hash),entry=index.scenes.find(s=>s.scene===target.scene);
    if(!entry){location.hash=storyHash(1);return;}
    const doc=await get(entry.file);if(token!==revision)return;
    const initial=!current;
    current=doc;selectedLine=target.line;phase='all';$('reader-status').textContent='';
    if(selectedLine){const line=doc.events.flatMap(e=>e.lines.map(l=>({e,l}))).find(({e,l})=>lineId(e.key,l.offset)===selectedLine);
      if(line&&!sectionVisible(line.l.section,$('route').value)){$('route').value='';savePreferences();$('reader-status').textContent='已恢复全部路线段，以显示定位的对白。';}}
    renderList();renderScene();$('error').hidden=true;
    try{localStorage.setItem('srw64.story.last',storyHash(doc.scene,selectedLine));}catch{}
    $('scene-list').querySelector('[aria-current=true]')?.scrollIntoView({block:'nearest'});
    if(selectedLine){const line=$('line-'+selectedLine);if(line){line.scrollIntoView({block:'center'});line.focus({preventScroll:true});}else $('reader-status').textContent='该定位句不在当前章节中，已显示章节内容。';}
    else if(!initial)document.querySelector('.chapter-controls').scrollIntoView({block:'start'});
    if($('search-scope').value==='scene'&&$('dialogue-search').value.trim().length>=2)search();
  }catch(e){if(token===revision)fail(e);}
}
for(const id of ['route','show-portraits','show-structure','show-source','font-size'])$(id).onchange=()=>{savePreferences();if(current)renderScene();};
$('scene-search').oninput=()=>{if(index)renderList();};
$('dialogue-search').oninput=()=>{clearTimeout(searchTimer);++searchRevision;searchTimer=setTimeout(search,180);};
$('search-scope').onchange=search;
$('search-clear').onclick=()=>{clearTimeout(searchTimer);++searchRevision;$('dialogue-search').value='';$('search-results').hidden=true;if(current)renderScene();};
$('event-jump').onchange=()=>{const key=$('event-jump').value;if(!key)return;phase='all';renderScene();$('event-'+key.split(':').pop())?.scrollIntoView({block:'start'});};
window.addEventListener('hashchange',route);
loadPreferences();
try{index=await get('story/index.json');if(!location.hash){let last='';try{last=localStorage.getItem('srw64.story.last')||'';}catch{}history.replaceState(null,'',last||storyHash(1));}await route();}catch(e){fail(e);}
