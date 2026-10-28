'use strict';
const DATA=window.ART_DATA, $=id=>document.getElementById(id);
const LABELS={unreviewed:'未审阅',overview:'已概览，待细查',suspect:'疑似问题',issue:'确认问题',pass:'细查通过'};
const KEY='srw64-art-review-v1';
let reviews={},server=false,current=null,kind='all',page=0,list=[],blinkHD=true,dirty=false;
const escapeHTML=s=>String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
const byId=new Map(DATA.rows.map(r=>[r.id,r]));
function decision(row){const r=reviews[row.id];return r&&r.hd_sha256===row.hd_sha256?r:{status:row.initial_status,note:row.initial_note,hd_sha256:row.hd_sha256};}
function cache(){try{localStorage.setItem(KEY,JSON.stringify(reviews));}catch(e){$('save-state').textContent='浏览器存储不可用，请导出记录';}}
function badge(text,cls=''){return `<span class="badge ${cls}">${escapeHTML(text)}</span>`;}
function render(){
 const query=$('search').value.trim().toLowerCase(),filter=$('status').value;
 list=DATA.rows.filter(r=>{const d=decision(r);return(kind==='all'||r.kind===kind)&&(!query||[r.id,r.name,...r.jp,d.note].join(' ').toLowerCase().includes(query))&&(filter==='all'||filter===d.status||(filter==='attention'&&['issue','suspect'].includes(d.status))||(filter==='technical'&&r.metrics.flags.length)||(filter==='candidate'&&r.candidate));});
 const priority=r=>({issue:0,suspect:1,unreviewed:3,overview:4,pass:5}[decision(r).status]-(r.metrics.flags.length?.2:0));
 list.sort((a,b)=>{const sort=$('sort').value;return(sort==='priority'?priority(a)-priority(b):sort==='delta'?b.metrics.color_delta-a.metrics.color_delta:sort==='iou'?a.metrics.iou-b.metrics.iou:0)||a.kind.localeCompare(b.kind)||a.id.localeCompare(b.id,undefined,{numeric:true});});
 page=Math.max(0,Math.min(page,Math.ceil(list.length/24)-1));
 $('grid').innerHTML=list.slice(page*24,page*24+24).map(r=>{const d=decision(r);return `<article class="card ${d.status}"><button class="pair" data-id="${r.id}" aria-label="查看 ${escapeHTML(r.name)}"><span class="tile"><img class="pixel" src="${r.original}" alt="ROM 原图" loading="lazy"><small>ROM</small></span><span class="tile"><img src="${r.hd}" alt="当前 HD" loading="lazy"><small>当前 HD</small></span></button><div class="meta"><div class="id">${r.kind==='unit'?'机体':'人物'} · ${r.id}</div><h3>${escapeHTML(r.name)}</h3><div class="badges">${badge(LABELS[d.status],d.status)}${d.category&&['issue','suspect'].includes(d.status)?badge(d.category):''}${r.metrics.flags.length?badge('技术提示 '+r.metrics.flags.length,'technical'):''}${r.candidate?badge('有修正候选'):''}</div>${d.note?`<p class="note">${escapeHTML(d.note)}</p>`:''}</div></article>`;}).join('')||'<p>没有符合条件的立绘。</p>';
 $('count').textContent=`筛选结果 ${list.length} / ${DATA.rows.filter(r=>kind==='all'||r.kind===kind).length}`;
 $('page-label').textContent=`${page+1} / ${Math.max(1,Math.ceil(list.length/24))}`;
 $('previous-page').disabled=page===0;$('next-page').disabled=(page+1)*24>=list.length;
 const counts={};DATA.rows.filter(r=>kind==='all'||r.kind===kind).forEach(r=>{const s=decision(r).status;counts[s]=(counts[s]||0)+1;});
 $('stats').innerHTML=[['机体',DATA.rows.filter(r=>r.kind==='unit').length],['人物',DATA.rows.filter(r=>r.kind==='portrait').length],['确认问题',counts.issue||0],['疑似问题',counts.suspect||0],['已概览，待细查',counts.overview||0],['未审阅',counts.unreviewed||0],['细查通过',counts.pass||0]].map(([label,n])=>`<div class="stat"><strong>${n}</strong><span>${label}</span></div>`).join('');
}
function tile(src,label,pixel=false){return `<div class="tile"><img ${pixel?'class="pixel"':''} src="${src}" alt="${label}"><small>${label}</small></div>`;}
function compare(){
 if(!current)return;const mode=$('view-mode').value, hd=$('candidate-toggle').checked&&current.candidate?current.candidate:current.hd,label=$('candidate-toggle').checked?'修正候选 · 未接入':'当前 HD';
 $('comparison').className=mode;
 $('comparison').innerHTML=mode==='side'?tile(current.original,'ROM 原图',true)+tile(hd,label):mode==='blink'?tile(blinkHD?hd:current.original,blinkHD?label:'ROM 原图',!blinkHD):`<div class="tile"><img src="${hd}" alt="${label}"><div class="overlay"><img class="pixel" src="${current.original}" alt="ROM 原图"></div><div class="divider"></div><small>左 ROM / 右 ${label}</small></div>`;
 $('wipe-control').hidden=mode!=='wipe';updateZoom();
}
function updateZoom(){$('comparison').style.setProperty('--zoom',$('zoom').value);$('zoom-label').textContent=$('zoom').value+'×';}
function openRow(row){current=row;const d=decision(row);dirty=false;$('v-id').textContent=row.id;$('v-name').textContent=row.name;$('decision').value=d.status;$('note').value=d.note||'';$('candidate-label').hidden=!row.candidate;$('candidate-toggle').checked=false;$('zoom').value=1;$('comparison').style.setProperty('--origin','50% 50%');$('review-feedback').textContent='';
 $('v-metrics').innerHTML=`<p>日文名：${escapeHTML(row.jp.join(' / ')||'—')}</p><p>ROM ${row.metrics.source_size.join(' × ')} → HD ${row.metrics.hd_size.join(' × ')}</p><p>轮廓 IoU：${row.metrics.iou} · 色彩差：${row.metrics.color_delta}</p><p>来源：${escapeHTML(row.recipe)}</p><p>当前文件：${escapeHTML(row.active_path)}</p>${row.metrics.flags.map(f=>`<p>${badge(f,'technical')}</p>`).join('')}${row.historical?.warnings?.length?`<p>历史提示：${escapeHTML(JSON.stringify(row.historical.warnings))}</p>`:''}${d.reviewer?`<p>检查人 / 方式：${escapeHTML(d.reviewer)}</p>`:''}${d.evidence&&/^(?:(?:unit|portrait)-audit-20261024\/)?sheets\/[a-zA-Z0-9.-]+$/.test(d.evidence)?`<p><a href="${escapeHTML(d.evidence)}" target="_blank" rel="noopener">打开检查对照图 ↗</a></p>`:''}`;
 const followup=d.face_followup;
 if(followup&&followup.hd_sha256===row.hd_sha256){
  $('v-metrics').insertAdjacentHTML('beforeend',`<p>面部专项复查：${escapeHTML(followup.assessment)}（静态图片）</p>`);
  if(/^face-repair-20261025\/audit\/(?:unit|portrait)-\d{2}\.jpg$/.test(followup.evidence))$('v-metrics').insertAdjacentHTML('beforeend',`<p><a href="${escapeHTML(followup.evidence)}" target="_blank" rel="noopener">打开本轮面部对照图 ↗</a></p>`);
 }
 compare();if(!$('viewer').open)$('viewer').showModal();location.hash=row.id;
}
async function persist(changes){
 Object.assign(reviews,changes);cache();
 if(server){try{const res=await fetch('api/reviews',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({reviews:changes})});if(!res.ok)throw Error(await res.text());$('save-state').textContent='已保存到本地文件';return true;}catch(e){$('save-state').textContent='文件保存失败；浏览器副本已保留，请导出';return false;}}
 $('save-state').textContent='已保存到本浏览器；可导出备份';return true;
}
async function saveCurrent(){if(!current)return;const d={...decision(current),status:$('decision').value,note:$('note').value,hd_sha256:current.hd_sha256,updated_at:new Date().toISOString(),reviewer:'网页人工审阅'};const ok=await persist({[current.id]:d});dirty=false;$('review-feedback').textContent=ok?'记录已保存':'浏览器已保存，文件保存失败，请导出备份';render();}
async function navigate(delta){if(dirty)await saveCurrent();const i=list.findIndex(r=>r.id===current.id),row=list[i+delta];if(row)openRow(row);}
function exportData(){const effective=Object.fromEntries(DATA.rows.map(r=>[r.id,{...decision(r),hd_sha256:r.hd_sha256}]));const blob=new Blob([JSON.stringify({schema:'srw64.art-decisions.v1',exported_at:new Date().toISOString(),reviews:effective},null,2)],{type:'application/json'});const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='srw64-art-reviews.json';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),1000);}
async function init(){
 $('scope').textContent=DATA.scope;
 try{reviews=JSON.parse(localStorage.getItem(KEY)||'{}');}catch(e){reviews={};}
 try{const res=await fetch('api/reviews');if(!res.ok)throw Error('offline');const saved=await res.json();if(!saved.reviews)throw Error('not review server');reviews={...reviews,...saved.reviews};server=true;$('save-state').textContent='已连接本地审阅文件';}catch(e){$('save-state').textContent='离线模式 · 浏览器保存 / JSON 导出';}
 render();const hash=decodeURIComponent(location.hash.slice(1));if(byId.has(hash))openRow(byId.get(hash));
}
 $('grid').addEventListener('click',e=>{const b=e.target.closest('[data-id]');if(b)openRow(byId.get(b.dataset.id));});
 $('kind').addEventListener('click',e=>{const b=e.target.closest('button');if(!b)return;kind=b.dataset.kind;for(const x of $('kind').children)x.setAttribute('aria-pressed',x===b);page=0;render();});
 for(const id of ['search','status','sort'])$(id).addEventListener(id==='search'?'input':'change',()=>{page=0;render();});
 $('background').onchange=()=>document.body.dataset.background=$('background').value;
 $('previous-page').onclick=()=>{page--;render();window.scrollTo({top:0});};$('next-page').onclick=()=>{page++;render();window.scrollTo({top:0});};
 $('close').onclick=async()=>{if(dirty)await saveCurrent();$('viewer').close();history.replaceState(null,'',location.pathname);};
 $('viewer').addEventListener('cancel',async e=>{e.preventDefault();$('close').click();});
 $('prev').onclick=()=>navigate(-1);$('next').onclick=()=>navigate(1);$('view-mode').onchange=compare;$('candidate-toggle').onchange=compare;
 $('zoom').oninput=updateZoom;$('reset-zoom').onclick=()=>{$('zoom').value=1;$('comparison').style.setProperty('--origin','50% 50%');updateZoom();};
 $('comparison').onclick=e=>{const tile=e.target.closest('.tile');if(!tile)return;const rect=tile.getBoundingClientRect();$('comparison').style.setProperty('--origin',`${100*(e.clientX-rect.left)/rect.width}% ${100*(e.clientY-rect.top)/rect.height}%`);if(Number($('zoom').value)===1){$('zoom').value=2;updateZoom();}};
 $('wipe').oninput=()=>$('comparison').style.setProperty('--wipe',$('wipe').value+'%');
 $('note').oninput=()=>{dirty=true;$('review-feedback').textContent='有未保存的修改';};$('decision').onchange=()=>{dirty=true;};$('save-review').onclick=saveCurrent;$('export').onclick=exportData;
 $('import').onchange=async e=>{try{const data=JSON.parse(await e.target.files[0].text());if(data.schema!=='srw64.art-decisions.v1'||!data.reviews||typeof data.reviews!=='object')throw Error('记录格式不正确');const changes={};let stale=0;for(const [id,d]of Object.entries(data.reviews)){if(!byId.has(id)||d.hd_sha256!==byId.get(id).hd_sha256){stale++;continue;}if(!LABELS[d.status]||typeof d.note!=='string')throw Error('状态或备注格式不正确');changes[id]=d;}await persist(changes);render();$('save-state').textContent=`已导入 ${Object.keys(changes).length} 条，跳过 ${stale} 条素材版本不匹配记录`;}catch(err){$('save-state').textContent='导入失败：'+err.message;}e.target.value='';};
 document.addEventListener('keydown',e=>{if(!$('viewer').open||['TEXTAREA','INPUT','SELECT'].includes(e.target.tagName))return;if(e.key==='ArrowLeft'){e.preventDefault();navigate(-1);}if(e.key==='ArrowRight'){e.preventDefault();navigate(1);}if(e.code==='Space'){e.preventDefault();blinkHD=!blinkHD;$('view-mode').value='blink';compare();}});
 window.addEventListener('beforeunload',e=>{if(dirty){e.preventDefault();e.returnValue='';}});
 init();
