import * as THREE from 'three';
import { OrbitControls } from 'three/addons/OrbitControls.js';

const $ = id => document.getElementById(id);
const wrap=$('canvas-wrap'),message=$('viewer-message');
let manifest, selected, modelData, modelGroup, filtered=[], filter='all', mode='texture', request=0, selectedVariant='original';
let renderer, scene, camera, controls, grid, modelScale=1, rendererError='';
const textureLoader=new THREE.TextureLoader(), textureCache=new Map(), geometryCache=new Map();
const statuses={unknown:'待定位',decoded:'静态已解析',located:'调用已定位',verified:'画面对照确认',prototype:'高模试作',live:'开场已验证'};

function initRenderer(){
  renderer=new THREE.WebGLRenderer({canvas:$('model-canvas'),antialias:true,alpha:true});
  renderer.setPixelRatio(Math.min(window.devicePixelRatio,2));
  renderer.outputColorSpace=THREE.SRGBColorSpace;
  scene=new THREE.Scene();
  camera=new THREE.PerspectiveCamera(38,1,.01,1000);
  controls=new OrbitControls(camera,renderer.domElement);
  controls.enableDamping=true;controls.dampingFactor=.09;controls.autoRotateSpeed=.8;
  controls.minDistance=.15;controls.maxDistance=70;
  scene.add(new THREE.HemisphereLight(0xc9e5f2,0x344657,2));
  const key=new THREE.DirectionalLight(0xffffff,2.3);key.position.set(3,5,4);scene.add(key);
  const rim=new THREE.DirectionalLight(0x8edbcc,1.4);rim.position.set(-4,2,-3);scene.add(rim);
  grid=new THREE.GridHelper(10,20,0x42606a,0x263d49);grid.material.transparent=true;grid.material.opacity=.38;scene.add(grid);
  new ResizeObserver(()=>{const w=wrap.clientWidth,h=wrap.clientHeight;renderer.setSize(w,h,false);camera.aspect=w/h;camera.updateProjectionMatrix();}).observe(wrap);
  renderer.setAnimationLoop(()=>{controls.update();renderer.render(scene,camera);});
  renderer.domElement.addEventListener('webglcontextlost',e=>{e.preventDefault();message.textContent='三维绘图上下文已中断，请刷新页面恢复。';message.hidden=false;});
}

async function loadTexture(path,wrapS,wrapT){
  if(!path)return null;
  const key=`${path}:${wrapS}:${wrapT}`;
  if(!textureCache.has(key))textureCache.set(key,textureLoader.loadAsync(path).then(t=>{
    t.colorSpace=THREE.SRGBColorSpace;t.flipY=false;t.magFilter=THREE.NearestFilter;t.minFilter=THREE.NearestFilter;
    t.wrapS=wrapS&2?THREE.ClampToEdgeWrapping:wrapS&1?THREE.MirroredRepeatWrapping:THREE.RepeatWrapping;
    t.wrapT=wrapT&2?THREE.ClampToEdgeWrapping:wrapT&1?THREE.MirroredRepeatWrapping:THREE.RepeatWrapping;
    return t;
  }));
  return textureCache.get(key);
}

function materialFor(batch,tex){
  const color=new THREE.Color(`rgb(${batch.color.slice(0,3).join(',')})`);
  if(mode==='texture'&&batch.shading==='native-gold')return new THREE.MeshPhongMaterial({color:0xd78b0a,specular:0xffefd1,shininess:75});
  if(mode==='texture'&&batch.shading==='unlit')return new THREE.MeshBasicMaterial({color,side:THREE.DoubleSide});
  if(mode==='texture')return tex?new THREE.MeshBasicMaterial({map:tex,color,side:THREE.DoubleSide,transparent:true,alphaTest:.03,depthWrite:false}):new THREE.MeshLambertMaterial({color,side:THREE.DoubleSide,flatShading:true});
  if(mode==='wire')return new THREE.MeshBasicMaterial({color:0x9bdcde,side:THREE.DoubleSide,wireframe:true});
  return new THREE.MeshStandardMaterial({color:0x89a6b7,metalness:.08,roughness:.72,side:THREE.DoubleSide,flatShading:batch.shading!=='native-gold'});
}

async function showModel(data,token){
  const group=new THREE.Group();
  const maps=await Promise.all(data.batches.map(b=>loadTexture(b.texture,b.wrap_s,b.wrap_t)));
  if(token!==request)return;
  data.batches.forEach((b,index)=>{
    const geometry=new THREE.BufferGeometry();geometry.setAttribute('position',new THREE.Float32BufferAttribute(b.positions,3));geometry.setAttribute('uv',new THREE.Float32BufferAttribute(b.uvs,2));
    if(b.normals)geometry.setAttribute('normal',new THREE.Float32BufferAttribute(b.normals,3));else geometry.computeVertexNormals();
    const mesh=new THREE.Mesh(geometry,materialFor(b,maps[index]));mesh.userData={part:b.part,batch:b,tex:maps[index]};mesh.renderOrder=index;
    const wire=new THREE.LineSegments(new THREE.WireframeGeometry(geometry),new THREE.LineBasicMaterial({color:0x193849,transparent:true,opacity:.75,depthTest:true}));
    wire.visible=$('wire-overlay').checked&&mode!=='wire';wire.userData.overlay=true;mesh.add(wire);group.add(mesh);
  });
  if(modelGroup){scene.remove(modelGroup);modelGroup.traverse(o=>{o.geometry?.dispose();if(o.material)o.material.dispose();});}
  modelGroup=group;scene.add(group);
  const bounds=new THREE.Box3().setFromObject(group),center=bounds.getCenter(new THREE.Vector3()),size=bounds.getSize(new THREE.Vector3());
  modelScale=3/Math.max(size.x,size.y,size.z,1);group.scale.setScalar(modelScale);group.position.copy(center).multiplyScalar(-modelScale);
  grid.position.y=(bounds.min.y-center.y)*modelScale-.04;grid.visible=$('show-grid').checked;
  setView('perspective');applyPart();message.hidden=true;
}

function setView(view){
  if(!camera)return;
  controls.target.set(0,0,0);camera.up.set(0,1,0);
  const distance=Math.max(5.8,3.2/Math.max(camera.aspect,.5));
  if(view==='top'){camera.position.set(0,distance,.001);camera.up.set(0,0,-1);}
  else if(view==='front')camera.position.set(0,.05,distance);
  else camera.position.set(distance*.62,distance*.4,distance*.7);
  controls.update();
}

function applyPart(){
  const part=$('part-select').value;
  modelGroup?.children.forEach(mesh=>mesh.visible=part==='all'||mesh.userData.part===Number(part));
}

function renderList(){
  const q=$('search').value.trim().toLowerCase();
  filtered=manifest.resources.filter(r=>(filter==='all'||filter==='space'&&r.rank===3||filter==='plane'&&r.rank===2||filter==='located'&&['located','verified'].includes(r.status))&&(`${r.id} ${r.title} ${r.note}`).toLowerCase().includes(q));
  $('resource-list').replaceChildren();
  const fragment=document.createDocumentFragment();
  for(const r of filtered){
    const button=document.createElement('button');button.className='resource-card';button.dataset.id=r.id;button.setAttribute('aria-current',String(r.id===selected?.id));button.setAttribute('aria-label',`${r.id} ${r.title}，${r.triangles} 个三角形`);
    const img=document.createElement('img');img.src=`thumbs/${r.id}.png`;img.alt='';img.loading='lazy';img.width=240;img.height=168;
    const caption=document.createElement('div');caption.className='card-caption';
    const id=document.createElement('span');id.className='card-id';id.textContent=r.id;
    if(['located','verified'].includes(r.status)){const mark=document.createElement('i');mark.textContent='●';id.append(mark);}
    const meta=document.createElement('div');meta.className='card-meta';meta.textContent=`${r.triangles.toLocaleString()} 面 · ${r.rank===2?'共面':'空间'}`;
    caption.append(id,meta);button.append(img,caption);button.addEventListener('click',()=>select(r.id));fragment.append(button);
  }
  $('resource-list').append(fragment);$('result-count').textContent=`${filtered.length} 项资源`;$('empty').hidden=filtered.length>0;
  updateNavigation();revealSelectedCard();
}

function updateNavigation(){
  const index=filtered.findIndex(r=>r.id===selected?.id);
  $('previous-resource').disabled=index<=0;
  $('next-resource').disabled=!filtered.length||index===filtered.length-1;
  $('resource-position').textContent=filtered.length?index<0?`未在筛选中 · ${filtered.length} 项`:`${index+1} / ${filtered.length}`:'没有匹配资源';
  $('previous-resource').title=index>0?`上一个：${filtered[index-1].id}（←）`:'已到列表开头';
  $('next-resource').title=filtered[index+1]?`下一个：${filtered[index+1].id}（→）`:'已到列表末尾';
}

function revealSelectedCard(){
  const list=$('resource-list'),card=list.querySelector('[aria-current="true"]');
  if(!card)return;
  const item=card.getBoundingClientRect(),viewport=list.getBoundingClientRect();
  if(item.top<viewport.top)list.scrollTop+=item.top-viewport.top-2;
  else if(item.bottom>viewport.bottom)list.scrollTop+=item.bottom-viewport.bottom+2;
}

function navigateResource(direction){
  const index=filtered.findIndex(r=>r.id===selected?.id),next=index+direction;
  if(next<0||next>=filtered.length)return;
  select(filtered[next].id);
}

function updateInspector(r){
  $('object-id').textContent=`RESOURCE ${r.id}`;$('object-name').textContent=r.title;$('detail-id').textContent=r.id;$('detail-name').textContent=r.title;$('detail-note').textContent=r.note;
  $('status').textContent=statuses[r.status];$('status').className=`status ${r.status}`;
  for(const [id,value] of Object.entries({triangles:r.triangles,vertices:r.vertices,parts:r.parts,textures:r.texture_count}))$(id).textContent=value.toLocaleString();
  $('bounds').replaceChildren();r.bounds.forEach((bound,i)=>{const row=document.createElement('div');const axis=document.createElement('b');axis.textContent='XYZ'[i];const value=document.createElement('span');value.textContent=`${bound[0]}  →  ${bound[1]}`;row.append(axis,value);$('bounds').append(row);});
  $('geometry-kind').textContent=r.rank===2?'绘制顶点位于同一平面。':'绘制顶点不共面；也可能由多个平面组合。';
  $('display-kind').textContent=r.rank===2?'共面几何':'空间几何';
  $('part-select').replaceChildren(new Option(`全部部件（${r.parts}）`,'all'));
  if(r.parts>1)for(let i=0;i<r.parts;i++)$('part-select').add(new Option(`部件 ${i+1}`,String(i)));
  $('part-select').disabled=r.parts===1;
  $('texture-note').textContent=r.texture_notes.length?'部分纹理装载方式尚未验证，已回退为纯色。已显示的源贴图采用简化材质；透明混合与光照可能不同于游戏。':'源贴图使用简化材质，透明混合和光照可能与游戏内不同。灰模和线框适合检查结构。';
  if(r.material_note)$('texture-note').textContent=r.material_note;
  $('resource-hash').textContent=r.source_note||`解压大小 ${r.bytes.toLocaleString()} 字节 · SHA-256 ${r.sha256}`;$('action-message').textContent='';
}

function updateVariants(base){
  const variants=base.variants||[];$('geometry-variants').hidden=!variants.length;
  $('variant-buttons').replaceChildren();
  if(!variants.length)return;
  for(const v of [{key:'original',label:base.original_variant_label||'原版'},...variants]){
    const button=document.createElement('button');button.textContent=v.label;button.dataset.variant=v.key;
    button.setAttribute('aria-pressed',String(v.key===selectedVariant));
    button.addEventListener('click',()=>select(base.id,true,v.key));$('variant-buttons').append(button);
  }
}

async function select(id,updateHash=true,variantKey='original'){
  const base=manifest.resources.find(r=>r.id===Number(id));if(!base)return;
  const variant=base.variants?.find(v=>v.key===variantKey);
  selectedVariant=variant?.key||'original';
  const r={...base,...variant?.overrides};
  selected=r;const token=++request;modelData=null;$('download-obj').disabled=true;updateInspector(r);
  updateVariants(base);
  document.querySelectorAll('.resource-card').forEach(b=>b.setAttribute('aria-current',String(Number(b.dataset.id)===r.id)));
  updateNavigation();revealSelectedCard();
  if(updateHash)history.replaceState(null,'',`#${r.id}`);
  message.textContent=`载入 ${r.id}…`;message.hidden=false;
  try{
    const path=variant?.file||`models/${r.id}.json`;
    if(!geometryCache.has(path))geometryCache.set(path,fetch(path).then(res=>{if(!res.ok)throw new Error('资源文件读取失败');return res.json();}));
    const data=await geometryCache.get(path);if(token!==request)return;modelData=data;
    if(renderer)await showModel(data,token);
    else {message.textContent=rendererError;message.hidden=false;}
    if(token===request)$('download-obj').disabled=false;
  }catch(error){if(token===request){message.textContent=`无法显示资源：${error.message}`;message.hidden=false;}}
}

function refreshMaterials(){
  document.querySelectorAll('[data-mode]').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.mode===mode)));
  modelGroup?.children.forEach(mesh=>{mesh.material.dispose();mesh.material=materialFor(mesh.userData.batch,mesh.userData.tex);mesh.children[0].visible=$('wire-overlay').checked&&mode!=='wire';});
}

function exportOBJ(){
  if(!modelData)return;
  const lines=[`# SRW64 resource ${selected.id}; local positions only; no materials or animation`];let next=1;
  const part=$('part-select').value;
  modelData.batches.forEach((b,i)=>{
    if(part!=='all'&&b.part!==Number(part))return;
    lines.push(`o resource_${selected.id}_part_${b.part+1}_batch_${i}`);
    for(let j=0;j<b.positions.length;j+=3)lines.push(`v ${b.positions.slice(j,j+3).join(' ')}`);
    for(let j=0;j<b.positions.length/3;j+=3)lines.push(`f ${next+j} ${next+j+1} ${next+j+2}`);
    next+=b.positions.length/3;
  });
  const url=URL.createObjectURL(new Blob([lines.join('\n')+'\n'],{type:'text/plain'}));const a=document.createElement('a');a.href=url;a.download=`srw64-${selected.id}${selectedVariant==='original'?'':'-'+selectedVariant}-geometry.obj`;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);
  $('action-message').textContent='已导出当前显示部件的原始坐标。';
}

function buildEvidence(){
  const images=manifest.evidence5600||[],live=manifest.liveEvidence5600,native=manifest.nativeEvidence5600;
  $('evidence-open').hidden=images.length===0&&!live&&!native;
  $('native-evidence').hidden=!native;
  if(native)$('native-evidence-scope').textContent=native.summary;
  $('live-evidence').hidden=!live;
  if(live)$('live-evidence-scope').textContent=live.summary;
  for(const [items,target] of [[images,'evidence-images'],[live?.frames||[],'live-evidence-images'],[native?.frames||[],'native-evidence-images']]){
    for(const item of items){const figure=document.createElement('figure');const a=document.createElement('a');a.href=item.path;a.target='_blank';a.rel='noopener';const img=document.createElement('img');img.src=item.path;img.alt=item.title;img.loading='lazy';a.append(img);const caption=document.createElement('figcaption');caption.textContent=item.title;figure.append(a,caption);$(target).append(figure);}
  }
}

$('search').addEventListener('input',()=>renderList());
document.querySelectorAll('[data-filter]').forEach(b=>b.addEventListener('click',()=>{filter=b.dataset.filter;document.querySelectorAll('[data-filter]').forEach(x=>x.setAttribute('aria-pressed',String(x===b)));renderList();}));
document.querySelectorAll('[data-mode]').forEach(b=>b.addEventListener('click',()=>{mode=b.dataset.mode;refreshMaterials();}));
document.querySelectorAll('[data-view]').forEach(b=>b.addEventListener('click',()=>setView(b.dataset.view)));
$('rotate').addEventListener('click',()=>{if(!controls)return;controls.autoRotate=!controls.autoRotate;$('rotate').setAttribute('aria-pressed',String(controls.autoRotate));});
$('reset-view').addEventListener('click',()=>setView('perspective'));
$('previous-resource').addEventListener('click',()=>navigateResource(-1));
$('next-resource').addEventListener('click',()=>navigateResource(1));
$('wire-overlay').addEventListener('change',refreshMaterials);$('show-grid').addEventListener('change',()=>{if(grid)grid.visible=$('show-grid').checked;});$('part-select').addEventListener('change',applyPart);
$('download-obj').addEventListener('click',exportOBJ);
$('copy-id').addEventListener('click',async()=>{try{await navigator.clipboard.writeText(String(selected.id));$('action-message').textContent=`已复制 ${selected.id}`;}catch{$('action-message').textContent=`资源编号：${selected.id}`;}});
$('evidence-open').addEventListener('click',()=>$('evidence-dialog').showModal());$('evidence-close').addEventListener('click',()=>$('evidence-dialog').close());
$('evidence-dialog').addEventListener('click',e=>{if(e.target===$('evidence-dialog')){const r=e.target.getBoundingClientRect();if(e.clientX<r.left||e.clientX>r.right||e.clientY<r.top||e.clientY>r.bottom)e.target.close();}});
window.addEventListener('hashchange',()=>select(location.hash.slice(1),false));
window.addEventListener('keydown',e=>{if(e.target.matches('input,select,textarea')||$('evidence-dialog').open)return;if(e.key==='/'){e.preventDefault();$('search').focus();}else if(e.key.toLowerCase()==='r'){setView('perspective');}else if(['ArrowLeft','ArrowRight'].includes(e.key)&&filtered.length){e.preventDefault();navigateResource(e.key==='ArrowRight'?1:-1);}});

try{
  const res=await fetch('manifest.json');if(!res.ok)throw new Error('目录读取失败');manifest=await res.json();$('total').textContent=manifest.resources.length;
  try{initRenderer();}catch(error){renderer=null;rendererError=`无法启动三维绘图：${error.message}。仍可浏览目录并导出几何。`;message.textContent=rendererError;message.hidden=false;}
  renderList();buildEvidence();await select(Number(location.hash.slice(1))||5600);
}catch(error){message.textContent=`载入失败：${error.message}`;message.hidden=false;$('result-count').textContent='目录不可用';}
