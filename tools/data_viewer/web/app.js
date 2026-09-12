import {element, anchor, block, disclosure, rawBlock} from './ui.js';
import {renderProfile, entityLinks} from './profiles.js';
import {renderAbility, abilityCoverage} from './abilities.js';
import {renderScript} from './scripts.js';
import {renderImages, thumbnail} from './images.js';
import {weaponBadges} from './weapons.js';

const $ = id => document.getElementById(id);
const names = {units:'机体',actors:'机师',unit_abilities:'机体特殊能力',pilot_skills:'人物特殊技能',weapons:'武器原表',pilot_stats:'能力原表',spirits:'精神原表',pilot_thresholds:'技能阈值原表',stage_maps:'场景→地图',map_assets:'地图资源',stages:'章节标题',observations:'第一话观察',texts:'原始文本',resources:'资源',actor_stats_map:'数值映射',actor_spirits_map:'精神映射',evidence:'解析证据'};
Object.assign(names, {scenarios:'关卡脚本',stage_events:'事件入口',stage_deployments:'出击记录',stage_auxiliary:'场景配套数据',script_opcodes:'脚本指令表',script_conditions:'脚本条件指令',script_markers:'脚本上下文标记',script_event_types:'事件触发类型'});
const primary = ['units','actors','map_assets','unit_abilities','pilot_skills','scenarios'];
names.map_assets = '地图';
const statusNames = {'structure-confirmed':'结构已确认','code-confirmed':'代码已确认','candidate':'候选 · 待确认','unknown':'尚未确认','snapshot-observed':'历史快照观察'};
let manifest, category = 'units', rows = [], filtered = [], page = 0, selected = '', revision = 0, categoryRevision = 0;
const pageSize = 50, cache = new Map();
const fmt = n => n.toLocaleString('en-US');
const hex = n => '0x' + n.toString(16).toUpperCase().padStart(8,'0');
function fail(error) {$('error').hidden=false;$('error').textContent='无法读取目录：'+error.message;}
async function get(path) {if (!cache.has(path)) cache.set(path, fetch(path).then(r=>{if(!r.ok)throw Error(path+' HTTP '+r.status);return r.json();}).catch(e=>{cache.delete(path);throw e;}));return cache.get(path);}
function catFor(key) {if(key.startsWith('base:t'))return 'texts';if(key.startsWith('evidence:'))return 'evidence';if(key.startsWith('candidate:'))return 'stages';if(key.startsWith('observation:'))return 'observations';return key.split(':')[1];}
function badge(status) {return element('span',statusNames[status]||status,'badge '+status);}
function valueText(value) {return typeof value==='object'?JSON.stringify(value,null,2):String(value);}
function filter() {const q=$('search').value.trim().toLocaleLowerCase();filtered=rows.filter(r=>(category!=='actors'||!$('defined-only').checked||r.has_stats)&&(r.key+' '+r.label+' '+(r.search_terms||'')+' '+(r.rom_offset===undefined?'':hex(r.rom_offset))).toLocaleLowerCase().includes(q));page=0;renderList();}
function renderList() {
  const count=Math.max(1,Math.ceil(filtered.length/pageSize));page=Math.max(0,Math.min(page,count-1));
  $('result-count').textContent=`${names[category]} · ${fmt(filtered.length)} / ${fmt(rows.length)} 条`;
  $('page-count').textContent=`${page+1} / ${count}`;$('page-prev').disabled=page===0;$('page-next').disabled=page===count-1;
  const fragment=document.createDocumentFragment();
  for(const row of filtered.slice(page*pageSize,(page+1)*pageSize)) {
    const b=element('button',undefined,'row');b.setAttribute('aria-current',String(row.key===selected));
    if(row.thumbnail){b.classList.add('with-thumbnail');b.append(thumbnail(row.thumbnail,row.label));}
    const text=element('div',undefined,'row-text');
    text.append(element('span',row.summary?'整合档案':statusNames[row.label_confidence||row.confidence],'status'),element('span',row.key,'key'),element('span',row.label.replaceAll('<END>','').replaceAll('<BR>',' / '),'label'));
    if(row.summary)text.append(element('small',row.summary,'row-summary'));
    b.append(text);b.onclick=()=>{location.hash=encodeURIComponent(row.key);};fragment.append(b);
  }
  if(!filtered.length)fragment.append(element('p','没有匹配记录。清空搜索可查看此分类全部记录。','empty'));
  $('rows').replaceChildren(fragment);$('rows').scrollTop=0;
  const index=filtered.findIndex(r=>r.key===selected);$('record-prev').disabled=index<=0;$('record-next').disabled=index<0||index>=filtered.length-1;
}
async function switchCategory(next) {
  if(!manifest.categories[next])throw Error('未知分类 '+next);
  const loadToken=++categoryRevision;
  category=next;$('search').value='';rows=[];filtered=[];page=0;renderList();
  $('defined-filter').hidden=next!=='actors';
  if(!primary.includes(next))$('source-categories').open=true;
  for(const b of document.querySelectorAll('button[data-category]'))b.setAttribute('aria-pressed',String(b.dataset.category===next));
  const data=await get(manifest.categories[next].index);
  if(category!==next||loadToken!==categoryRevision)return false;rows=data;filter();return true;
}
async function show(key) {
  const token=++revision;const next=catFor(key);
  if(category!==next||!rows.length)await switchCategory(next);
  if(token!==revision)return;
  const row=rows.find(r=>r.key===key);if(!row)throw Error('未知记录 '+key);
  if(!filtered.some(r=>r.key===key)) {
    $('search').value='';
    if(category==='actors'&&!row.has_stats)$('defined-only').checked=false;
    filter();
  }
  selected=key;const index=filtered.findIndex(r=>r.key===key);if(index>=0)page=Math.floor(index/pageSize);renderList();
  $('rows').querySelector('[aria-current=true]')?.scrollIntoView({block:'nearest'});
  $('detail').replaceChildren(element('p','正在读取记录…','empty'));
  const data=(await get(row.detail_file))[key];if(token!==revision)return;
  const article=$('detail');article.replaceChildren(element('div',data.key,'mono'),element('h2',data.text_ir?'原始文本 '+data.text_ir.key:data.label));
  if(data.images)article.append(renderImages(data));
  if(data.weapon_traits)article.append(weaponBadges(data.weapon_traits, true));
  if(data.profile || data.definition || data.scenario || data.script || data.opcode || data.event_type || data.auxiliary || data.deployment) {
    if(data.scenario || data.script || data.opcode || data.event_type || data.auxiliary || data.deployment) article.append(renderScript(data, manifest.script_coverage));
    else if(data.definition) article.append(abilityCoverage(category, manifest.ability_coverage), renderAbility(data));
    else article.append(renderProfile(data));
    const raw=element('div');renderRaw(raw,data);
    article.append(disclosure('原始字段、字节与解析证据',raw,'technical-details'));
  } else {
    article.append(badge(data.confidence));
    if(data.related_entities?.length)article.append(block('相关整合档案',entityLinks(data.related_entities)));
    renderRaw(article,data);
  }
  article.scrollTop=0;$('error').hidden=true;
}
function renderRaw(article,data) {
  if(data.label_confidence)article.append(document.createTextNode('名称关联：'),badge(data.label_confidence));
  if(data.menu_label)article.append(block('菜单中的名称',element('div',data.menu_label,'source-text')));
  if(data.rom_offset!==undefined) {const dl=element('dl',undefined,'meta');for(const [label,v] of [['ROM 地址',hex(data.rom_offset)],['记录长度',`${data.byte_size} bytes`],['SHA-256',data.source_sha256]])dl.append(element('dt',label),element('dd',v,'mono'));article.append(dl);}
  if(data.text_ir) article.append(block('原文与控制标记',element('div',data.label.replaceAll('<BR>','<BR>\n'),'source-text')));
  if(data.fields?.length) {const fields=element('div');for(const f of data.fields) {const d=element('div',undefined,'field');d.append(element('div',f.name,'field-name'),element('div',valueText(f.value)+(f.sentinel_meaning?' · '+f.sentinel_meaning:''),'field-value'),badge(f.confidence));
    const encoding=[];
    if(f.encoded_value!==undefined)encoding.push(`原始 ${f.encoding}${f.size*8}：${f.encoded_value}${f.scale!==1?' × '+f.scale:''}`);
    if(f.growth_per_level!==undefined)encoding.push(`从等级 1 起，每升一级 +${f.growth_per_level}`);
    if(encoding.length)d.append(element('div',encoding.join(' · '),'basis'));
    if(f.basis)d.append(element('div',f.basis,'basis'));fields.append(d);}article.append(block('字段与解释',fields));}
  if(data.instances){const list=element('div');for(const u of data.instances){const d=element('div',undefined,'instance');d.append(element('small',`bank ${u.bank} · slot ${u.slot} · ${u.vram}`),anchor(u.unit_key,u.label));const pilots=element('div');for(const p of u.pilots) {pilots.append(document.createTextNode(' → '),anchor(p.actor_key,p.label));}d.append(pilots);list.append(d);}article.append(block('机体与驾驶员 · 包含备用形态',list));}
  if(data.links?.length) {const list=element('div',undefined,'links');for(const l of data.links){const a=anchor(l.key,l.relation+' → '+l.key);a.className='link';a.append(element('small',statusNames[l.confidence]||l.confidence));list.append(a);}article.append(block('关联记录',list));}
  if(data.decoded_file){const a=element('a','下载解压后的原始资源 ↗','download');a.href=data.decoded_file;a.download='';article.append(block('资源文件',a));}
  if(data.raw_hex)article.append(block(data.raw_preview_only?'原始压缩块 · 前 64 字节':'原始字节',element('pre',data.raw_hex.match(/.{1,32}/g)?.map(x=>x.match(/.{1,2}/g).join(' ')).join('\n')||'')));
  if(data.weapon_list){const list=element('div',undefined,'links');for(const w of data.weapon_list.rows){const d=element('div',undefined,'field');d.append(anchor(`base:weapons:${String(w.weapon_id).padStart(4,'0')}`,`武器 ${w.weapon_id}`),element('small','匹配机体形态：'));for(const id of w.eligible_unit_ids)d.append(anchor(`base:units:${String(id).padStart(4,'0')}`,String(id)),document.createTextNode(' '));list.append(d);}article.append(block('武器与机体形态 · 匹配不代表满足全部使用条件',list),rawBlock('展开机体武器列表（保留原始五槽与 FFFF）',JSON.stringify(data.weapon_list,null,2)));}
  article.append(rawBlock('展开完整记录 JSON',JSON.stringify(data,null,2)));
}
async function route(){try {if(location.hash)await show(decodeURIComponent(location.hash.slice(1)));}catch(e){fail(e);}}
$('page-prev').onclick=()=>{page--;renderList();};$('page-next').onclick=()=>{page++;renderList();};
for(const [id,step] of [['record-prev',-1],['record-next',1]])$(id).onclick=()=>{const i=filtered.findIndex(r=>r.key===selected);if(filtered[i+step])location.hash=encodeURIComponent(filtered[i+step].key);};
let timer;$('search').oninput=()=>{clearTimeout(timer);timer=setTimeout(filter,120);};
$('defined-only').onchange=filter;
window.addEventListener('hashchange',route);
try {
  manifest=await get('manifest.json');
  $('stats').replaceChildren(...[['units','机体档案'],['actors','机师／人物身份']].map(([k,n])=>{const d=element('div');d.append(element('strong',fmt(manifest.counts[k])),element('small',n));return d;}));
  $('limits').replaceChildren(...manifest.limits.map(s=>element('li',s)));
  for(const [k,name] of Object.entries(names)){if(!manifest.categories[k])continue;const b=element('button',name);b.dataset.category=k;b.append(element('b',fmt(manifest.counts[k])));b.onclick=async()=>{try {++revision;if(primary.includes(k))$('source-categories').open=false;if(await switchCategory(k)){selected='';$('detail').replaceChildren(element('p','选择记录，查看关联数据。','empty'));if(filtered.length){const hash='#'+encodeURIComponent(filtered[0].key);if(location.hash===hash)await show(filtered[0].key);else location.hash=hash;}}}catch(e){fail(e);}};$(primary.includes(k)?'categories':'raw-categories').append(b);}
  if(!location.hash)location.hash=encodeURIComponent('base:units:0036');else await route();
}catch(e){fail(e);}
