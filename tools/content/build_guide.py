#!/usr/bin/env python3
"""Render the offline single-page guide (Chinese, Japanese, English) from guide/data/<lang>/*.json.

The page follows the sister project's guide (srwz-zh/guide/srwz-z-flow-guide.html):
a sticky tab bar, a flow chart of stage cards grouped by stage number, filterable
hidden-element cards, and reference tabs made of tables and lists. Everything is
inlined so the file opens straight from disk.

All three languages live in one self-contained file; the page shows one at a time and
the language switch keeps the reader's place (same anchors in every language). Text may carry three markers that render as badges: {≠} the
finding differs from Akurasu, {+} Akurasu does not record it, {?} not yet verified
in a running game.

    python3 tools/content/build_guide.py            # writes guide/srw64-flow-guide.html
    python3 tools/content/build_guide.py --check    # fails when a page is stale
"""
from __future__ import annotations

import argparse
import html
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "guide" / "data"
GUIDE = ROOT / "guide"
LANGS = ("zh-Hans", "ja", "en")

CSS = r"""
:root{color-scheme:light;--background:#f8fafc;--foreground:#0f172a;--card:#fff;--muted:#64748b;--muted-strong:#475569;--muted-bg:#f1f5f9;--border:#e2e8f0;--border-strong:#cbd5e1;--dot:#94a3b8;--primary:#0f172a;--on-primary:#fff;--tab-bg:rgba(255,255,255,.94);
--blue:#2563eb;--blue-ink:#1d4ed8;--blue-bg:#eff6ff;--blue-line:#bfdbfe;--green:#15803d;--green-bg:#f0fdf4;--amber:#a16207;--amber-ink:#92400e;--amber-bg:#fffbeb;--amber-line:#fde68a;--red:#b91c1c;--red-ink:#7f1d1d;--red-bg:#fef2f2;--red-line:#fecaca;--violet:#7c3aed;--violet-bg:#f5f3ff;--orange:#c2410c;--orange-bg:#fff7ed;--orange-line:#fed7aa;--teal:#0f766e;--teal-bg:#f0fdfa;--ring:rgba(37,99,235,.18);--shadow:0 1px 2px rgba(15,23,42,.04),0 8px 24px rgba(15,23,42,.04)}
@media (prefers-color-scheme:dark){:root:not([data-theme="light"]){color-scheme:dark;--background:#0b1120;--foreground:#e2e8f0;--card:#111827;--muted:#94a3b8;--muted-strong:#cbd5e1;--muted-bg:#1e293b;--border:#1f2a3c;--border-strong:#334155;--dot:#64748b;--primary:#e2e8f0;--on-primary:#0b1120;--tab-bg:rgba(11,17,32,.92);
--blue:#60a5fa;--blue-ink:#93c5fd;--blue-bg:#172554;--blue-line:#1e40af;--green:#4ade80;--green-bg:#052e16;--amber:#fbbf24;--amber-ink:#fcd34d;--amber-bg:#2a1f05;--amber-line:#713f12;--red:#f87171;--red-ink:#fecaca;--red-bg:#2a0f12;--red-line:#7f1d1d;--violet:#a78bfa;--violet-bg:#1e1538;--orange:#fb923c;--orange-bg:#2a1406;--orange-line:#7c2d12;--teal:#2dd4bf;--teal-bg:#042f2e;--ring:rgba(96,165,250,.3);--shadow:none}}
:root[data-theme="dark"]{color-scheme:dark;--background:#0b1120;--foreground:#e2e8f0;--card:#111827;--muted:#94a3b8;--muted-strong:#cbd5e1;--muted-bg:#1e293b;--border:#1f2a3c;--border-strong:#334155;--dot:#64748b;--primary:#e2e8f0;--on-primary:#0b1120;--tab-bg:rgba(11,17,32,.92);
--blue:#60a5fa;--blue-ink:#93c5fd;--blue-bg:#172554;--blue-line:#1e40af;--green:#4ade80;--green-bg:#052e16;--amber:#fbbf24;--amber-ink:#fcd34d;--amber-bg:#2a1f05;--amber-line:#713f12;--red:#f87171;--red-ink:#fecaca;--red-bg:#2a0f12;--red-line:#7f1d1d;--violet:#a78bfa;--violet-bg:#1e1538;--orange:#fb923c;--orange-bg:#2a1406;--orange-line:#7c2d12;--teal:#2dd4bf;--teal-bg:#042f2e;--ring:rgba(96,165,250,.3);--shadow:none}
*{box-sizing:border-box}html{scroll-behavior:smooth;background:var(--background)}body{margin:0;background:var(--background);color:var(--foreground);font-family:Inter,-apple-system,BlinkMacSystemFont,"Segoe UI","PingFang SC","Noto Sans CJK SC","Microsoft YaHei",sans-serif;line-height:1.55;-webkit-font-smoothing:antialiased}
a{color:inherit;text-decoration:none}button{font:inherit}
.mode-tabs{position:sticky;top:0;z-index:20;display:flex;height:52px;overflow-x:auto;background:var(--tab-bg);border-bottom:1px solid var(--border);backdrop-filter:blur(12px);scrollbar-width:none}.mode-tabs::-webkit-scrollbar{display:none}
.mode-tab{flex:1 0 auto;min-width:88px;padding:0 12px;display:flex;align-items:center;justify-content:center;color:var(--muted);font-size:.88rem;font-weight:650;white-space:nowrap;border-bottom:2px solid transparent;transition:.15s ease}.mode-tab:hover{background:var(--muted-bg);color:var(--foreground)}.mode-tab.active{color:var(--foreground);border-bottom-color:var(--foreground)}
main{width:min(1280px,calc(100% - 32px));margin:22px auto 64px}.guide-panel{scroll-margin-top:62px}.guide-panel[hidden]{display:none}
.page-head{display:flex;flex-wrap:wrap;align-items:flex-end;justify-content:space-between;gap:10px 20px;margin:0 0 18px}.page-head h1{margin:0;font-size:1.25rem;letter-spacing:-.01em}.page-head p{margin:3px 0 0;color:var(--muted);font-size:.8rem}
.legend{display:flex;flex-wrap:wrap;gap:6px 14px;margin:0;padding:0;list-style:none;color:var(--muted);font-size:.72rem}.legend li{display:flex;align-items:center;gap:6px}
.flow-intro{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin:0 0 30px}.intro-card{padding:16px 18px;background:var(--card);border:1px solid var(--border);border-radius:12px;box-shadow:var(--shadow)}.intro-card h2{margin:0 0 8px;font-size:.8rem;color:var(--muted);letter-spacing:.06em}.intro-card ul{list-style:none;margin:0;padding:0}.intro-card li{position:relative;padding:4px 0 4px 13px;font-size:.81rem}.intro-card li::before{content:"";position:absolute;left:1px;top:.78rem;width:3px;height:3px;border-radius:50%;background:var(--dot)}
.route-map{grid-column:1/-1}.route-map{overflow-x:auto}.route-map svg{display:block;width:100%;min-width:720px;height:auto}.route-map .rm-box{fill:var(--muted-bg);stroke:var(--border-strong)}.route-map .rm-ia{fill:var(--blue-bg);stroke:var(--blue-line)}.route-map .rm-oz{fill:var(--orange-bg);stroke:var(--orange-line)}.route-map .rm-cp{fill:var(--green-bg);stroke:var(--green)}.route-map .rm-end{fill:var(--violet-bg);stroke:var(--violet)}.route-map text{fill:var(--foreground);font-size:12px}.route-map .rm-sub{fill:var(--muted);font-size:10.5px}.route-map .rm-edge{stroke:var(--dot);fill:none;stroke-width:1.4}.route-map .rm-label{fill:var(--muted);font-size:10px}
.flow-section{margin:0 0 42px;scroll-margin-top:62px}.flow-section h2{margin:0 0 12px;padding:0 2px 9px;border-bottom:1px solid var(--border);font-size:.8rem;line-height:1.2;letter-spacing:.08em;color:var(--muted);font-weight:700}
.flow-row{--lane-min:300px;display:grid;grid-template-columns:56px 1fr;gap:10px;margin:0 0 10px;align-items:stretch}.flow-row.many{--lane-min:230px}
.stage-number{position:sticky;top:62px;align-self:start;min-height:78px;border:1px solid var(--border);border-radius:10px;background:var(--card);display:flex;flex-direction:column;align-items:center;justify-content:center;line-height:1}.stage-number strong{font-size:1.35rem;font-variant-numeric:tabular-nums}.stage-number span{font-size:.65rem;color:var(--muted);margin:2px 0}
.stage-lanes{display:grid;grid-template-columns:repeat(auto-fit,minmax(min(100%,var(--lane-min)),1fr));gap:10px}
.stage-card,.secret-card{background:var(--card);border:1px solid var(--border);border-radius:12px;box-shadow:var(--shadow)}.stage-card{overflow:hidden;scroll-margin-top:62px;min-width:0}
.stage-header{padding:14px 16px 12px;border-bottom:1px solid var(--border)}
.lane{display:inline-flex;align-items:center;min-height:22px;padding:2px 8px;border:1px solid var(--blue-line);border-radius:999px;background:var(--blue-bg);color:var(--blue-ink);font-size:.7rem;font-weight:650}.lane.oz{border-color:var(--orange-line);background:var(--orange-bg);color:var(--orange)}.lane.cp{border-color:var(--green);background:var(--green-bg);color:var(--green)}.lane.common{border-color:var(--border-strong);background:var(--muted-bg);color:var(--muted-strong)}.lane.super{border-color:var(--red-line);background:var(--red-bg);color:var(--red)}
.stage-card h3,.secret-card h3{margin:7px 0 0;font-size:1rem;line-height:1.35;letter-spacing:-.01em}.source-title{margin:2px 0 0;color:var(--muted);font-size:.75rem}
.conditions{display:grid;grid-template-columns:auto 1fr;gap:3px 9px;margin:9px 0 0;font-size:.76rem}.conditions dt{color:var(--muted);font-weight:650}.conditions dd{margin:0}
.stage-content{padding:4px 16px 10px}.stage-block{padding:11px 0;border-top:1px solid var(--border)}.stage-block:first-child{border-top:0}
.stage-block h4{display:flex;align-items:center;gap:7px;margin:0 0 6px;font-size:.76rem;line-height:1.25;font-weight:700;color:var(--muted)}.block-dot{width:7px;height:7px;border-radius:999px;background:var(--muted)}
.acquisition h4{color:var(--blue)}.acquisition .block-dot{background:var(--blue)}.temporary h4{color:var(--violet)}.temporary .block-dot{background:var(--violet)}.availability h4{color:var(--orange)}.availability .block-dot{background:var(--orange)}.upgrade h4{color:var(--green)}.upgrade .block-dot{background:var(--green)}.parts h4{color:var(--teal)}.parts .block-dot{background:var(--teal)}.hidden-progress h4{color:var(--amber)}.hidden-progress .block-dot{background:var(--amber)}
.info-list,.hidden-list{list-style:none;padding:0;margin:0}.info-list li,.hidden-list li{position:relative;padding:4px 0 4px 13px;font-size:.81rem;line-height:1.55}.info-list li::before,.hidden-list li::before{content:"";position:absolute;left:1px;top:.78rem;width:3px;height:3px;border-radius:50%;background:var(--dot)}
.part-from{color:var(--muted)}.part-note{color:var(--muted);font-size:.74rem}
.hidden-list a{color:var(--amber-ink);font-weight:650;text-decoration:underline;text-decoration-color:var(--amber-line);text-underline-offset:3px}.hidden-list p{margin:2px 0 0;color:var(--muted-strong)}
.script-tag{display:inline-flex;margin-left:6px;padding:0 5px;border:1px solid var(--red-line);border-radius:999px;background:var(--red-bg);color:var(--red);font-size:.6rem;font-weight:700;vertical-align:1px;white-space:nowrap}
.notes li{color:var(--muted-strong)}

.category-filters{position:sticky;top:52px;z-index:10;display:flex;flex-wrap:wrap;gap:7px;margin:0 0 14px;padding:10px 0;background:linear-gradient(var(--background) 78%,transparent)}.category-filters button{min-height:34px;padding:5px 11px;border:1px solid var(--border);border-radius:8px;background:var(--card);color:var(--muted);cursor:pointer;font-size:.78rem;font-weight:600}.category-filters button:hover{color:var(--foreground);border-color:var(--border-strong)}.category-filters button.active{background:var(--primary);color:var(--on-primary);border-color:var(--primary)}.category-filters button:focus-visible,.mode-tab:focus-visible{outline:3px solid var(--ring);outline-offset:-2px}
.secret-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}.secret-card{padding:17px 18px;scroll-margin-top:106px;min-width:0}.secret-card:target{outline:2px solid var(--amber);outline-offset:2px}
.secret-kicker{display:inline-flex;padding:2px 7px;border-radius:999px;background:var(--amber-bg);color:var(--amber);font-size:.68rem;font-weight:700}.secret-applies{display:inline-flex;margin-left:6px;color:var(--muted);font-size:.7rem}
.secret-summary{color:var(--muted);font-size:.82rem;margin:7px 0 12px}
.secret-steps{padding:0;margin:0;list-style:none;counter-reset:secret-step}.secret-steps>li{position:relative;padding:10px 0 10px 38px;border-top:1px solid var(--border);counter-increment:secret-step}.secret-steps>li::before{content:counter(secret-step);position:absolute;left:0;top:10px;display:grid;place-items:center;width:24px;height:24px;border:1px solid var(--border);border-radius:7px;background:var(--muted-bg);color:var(--muted);font-size:.7rem;font-weight:700}.secret-steps p{margin:4px 0 0;font-size:.82rem}
.step-head{display:flex;gap:7px;align-items:center;flex-wrap:wrap}.when{display:inline-flex;padding:1px 7px;border-radius:999px;background:var(--blue-bg);color:var(--blue-ink);font-size:.68rem;font-weight:700}.step-stage{font-size:.76rem;font-weight:650}.step-route{color:var(--muted);font-size:.7rem}
.secret-result{margin:4px 0 0;padding:9px 11px;border-radius:8px;background:var(--green-bg);color:var(--green);font-size:.8rem;font-weight:600}
.secret-notes{list-style:none;margin:10px 0 0;padding:0}.secret-notes li{position:relative;padding:3px 0 3px 13px;color:var(--muted-strong);font-size:.78rem}.secret-notes li::before{content:"";position:absolute;left:1px;top:.72rem;width:3px;height:3px;border-radius:50%;background:var(--dot)}
.correction{margin:10px 0 0;padding:10px 11px;border:1px solid var(--red-line);border-radius:8px;background:var(--red-bg);color:var(--red-ink)}.correction-title{margin:0 0 4px;color:var(--red);font-size:.72rem;font-weight:750}.correction ul{list-style:none;margin:0;padding:0}.correction li{padding:2px 0;font-size:.78rem}
.is-hidden{display:none!important}.hidden-intro{margin:0 0 14px}
.tag{display:inline-flex;margin-left:6px;padding:0 5px;border:1px solid;border-radius:999px;font-size:.6rem;font-weight:700;vertical-align:1px;white-space:nowrap}.tag.diff{border-color:var(--red-line);background:var(--red-bg);color:var(--red)}.tag.new{border-color:var(--blue-line);background:var(--blue-bg);color:var(--blue-ink)}.tag.unverified{border-color:var(--amber-line);background:var(--amber-bg);color:var(--amber)}
.lang-switch{position:sticky;right:0;display:flex;align-items:center;gap:2px;margin-left:auto;padding:0 10px;flex:0 0 auto;background:var(--tab-bg);box-shadow:-10px 0 10px -6px var(--tab-bg)}.lang-switch a{padding:4px 8px;border-radius:6px;color:var(--muted);font-size:.74rem;font-weight:650;white-space:nowrap}.lang-switch a:hover{background:var(--muted-bg);color:var(--foreground)}.lang-switch a[aria-current]{background:var(--primary);color:var(--on-primary)}
.tag-legend{display:flex;flex-wrap:wrap;gap:4px 12px;margin:6px 0 0;padding:0;list-style:none;color:var(--muted);font-size:.72rem}.tag-legend .tag{margin-left:0;margin-right:4px}
.reference-shell{display:grid;grid-template-columns:1fr;gap:12px;max-width:1120px;margin:auto}.reference-lead{margin:0 0 4px;color:var(--muted);font-size:.84rem}
.reference-card{padding:18px;background:var(--card);border:1px solid var(--border);border-radius:12px;box-shadow:var(--shadow);min-width:0}.reference-card h2{margin:0 0 12px;font-size:.86rem;color:var(--muted);letter-spacing:.04em}
.table-wrap{overflow-x:auto;border:1px solid var(--border);border-radius:10px}.reference-table{width:100%;border-collapse:collapse;font-size:.79rem}.reference-table th{padding:9px 11px;background:var(--muted-bg);color:var(--muted);font-size:.7rem;text-align:left;white-space:nowrap}.reference-table td{padding:9px 11px;border-top:1px solid var(--border);vertical-align:top;white-space:pre-line}.reference-table td.num{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}
.note-list{list-style:none;padding:0;margin:10px 0 0}.note-list li{position:relative;padding:5px 0 5px 15px;font-size:.8rem;color:var(--muted-strong)}.note-list li::before{content:"";position:absolute;left:1px;top:.8rem;width:4px;height:4px;border-radius:99px;background:var(--dot)}
.list-block li{color:var(--foreground)}
.site-foot{margin:40px 0 0;padding-top:14px;border-top:1px solid var(--border);color:var(--muted);font-size:.72rem}
@media(max-width:900px){.secret-grid,.flow-intro{grid-template-columns:1fr}}
@media(max-width:620px){.mode-tab{flex-basis:78px;font-size:.78rem}main{width:calc(100% - 32px);margin-top:14px}.flow-row{grid-template-columns:42px 1fr;gap:7px}.stage-number{top:59px;min-height:68px;border-radius:9px}.stage-number strong{font-size:1.05rem}.stage-header{padding:12px 13px 10px}.stage-content{padding:3px 13px 8px}.secret-card{padding:15px}.reference-card{padding:14px}.reference-table{font-size:.75rem}}
@media print{.mode-tabs,.category-filters{display:none}body{background:white}main{width:100%;margin:0}.guide-panel[hidden]{display:block}.stage-card,.secret-card{box-shadow:none;break-inside:avoid}.stage-number{position:static}}
"""

SCRIPT = r"""
(()=>{const roots=[...document.querySelectorAll('.lang-root')];const langs=roots.map(r=>r.dataset.lang);let lang=null;
function pick(){const q=new URLSearchParams(location.search).get('lang');if(langs.includes(q))return q;try{const s=localStorage.getItem('srw64-guide-lang');if(langs.includes(s))return s;}catch(e){}const n=(navigator.language||'').toLowerCase();if(n.startsWith('ja'))return'ja';if(n.startsWith('zh'))return'zh-Hans';return langs.includes('en')?'en':langs[0];}
function root(){return roots.find(r=>r.dataset.lang===lang);}
function find(id){return document.getElementById(lang+'--'+id);}
function panelFor(hash,names){if(hash.startsWith('#secret-'))return'hidden-elements';if(hash.startsWith('#card-')||hash.startsWith('#flow-'))return'flow';const n=hash.slice(1);return names.has(n)?n:'flow';}
function select(keep){const r=root();const panels=[...r.querySelectorAll('[data-panel-content]')];const tabs=[...r.querySelectorAll('[data-panel]')];const names=new Set(panels.map(p=>p.dataset.panelContent));const hash=location.hash;const name=panelFor(hash,names);
panels.forEach(p=>p.hidden=p.dataset.panelContent!==name);tabs.forEach(t=>t.classList.toggle('active',t.dataset.panel===name));tabs.find(t=>t.dataset.panel===name)?.scrollIntoView({inline:'center',block:'nearest'});
if(hash.startsWith('#secret-')){const card=find(hash.slice(1));if(card&&card.classList.contains('is-hidden'))category('all');}
if(keep)return;if(hash.length>1&&!names.has(hash.slice(1)))requestAnimationFrame(()=>find(hash.slice(1))?.scrollIntoView());else window.scrollTo(0,0);}
function category(c){const r=root();r.querySelectorAll('button[data-category]').forEach(b=>b.classList.toggle('active',b.dataset.category===c));r.querySelectorAll('.secret-card').forEach(card=>card.classList.toggle('is-hidden',c!=='all'&&card.dataset.category!==c));}
function setLang(l){const y=window.scrollY;const before=lang;lang=l;roots.forEach(r=>r.hidden=r.dataset.lang!==l);document.documentElement.lang=l;document.title=root().dataset.title;document.querySelectorAll('.lang-switch a').forEach(a=>{if(a.dataset.lang===l)a.setAttribute('aria-current','true');else a.removeAttribute('aria-current');});try{localStorage.setItem('srw64-guide-lang',l);}catch(e){}if(before&&new URLSearchParams(location.search).has('lang')){try{history.replaceState(null,'',location.pathname+'?lang='+l+location.hash);}catch(e){}}select(true);if(before){const h=location.hash;if(h.length>1&&!root().querySelector('[data-panel-content="'+h.slice(1)+'"]'))find(h.slice(1))?.scrollIntoView();else window.scrollTo(0,y);}}
document.querySelectorAll('.lang-switch a').forEach(a=>a.addEventListener('click',e=>{e.preventDefault();setLang(a.dataset.lang);}));
document.querySelectorAll('button[data-category]').forEach(b=>b.addEventListener('click',()=>category(b.dataset.category)));
window.addEventListener('hashchange',()=>select(false));setLang(pick());select(false);})();
"""

# Page chrome per language. Data text comes from guide/data/<lang>/.
UI = {
    "zh-Hans": {
        "switch": "中文",
        "title": "《超级机器人大战64》流程、隐藏要素与资料攻略", "nav": "攻略页面",
        "flow": "流程图", "hidden": "隐藏要素",
        "flow_lead": "按话数排列；同一话数里并排的卡片是不同主角或路线的关卡。话数按游戏脚本的关卡衔接推算，取各路线最常见的值；走法不同时实际话数会前后差几话，卡片第一条备注写明差别。",
        "hidden_lead": "人物、机体、武器与路线分歧的达成条件。条件以本项目对游戏脚本的解析为准，与社区攻略 Akurasu 的差异逐条标出。",
        "legend": ["加入", "临时参战", "离队", "强化", "部件", "隐藏要素"],
        "tags": {"diff": "与 Akurasu 不同", "new": "Akurasu 未载", "unverified": "未实机验证"},
        "route_map": "路线总览", "victory": "胜利", "defeat": "败北", "after": "过关后：",
        "b_join": "加入／取得", "b_temp": "临时参战", "b_leave": "离队／无法使用", "b_upgrade": "强化／换机／新武器",
        "b_parts": "强化部件（击坠掉落）", "b_secret": "隐藏要素与分歧", "b_notes": "备注",
        "stage": ("第", "话"), "step": "第 {} 话", "all": "全部", "akurasu_box": "与 Akurasu 的差异",
        "sources": "资料来源：", "sep": "；", "paren": "（{}）",
        "map": [("开局", "四主角·真实／超级系"), ("共通路线", "至破晓作战"), ("OZ 路线", "破晓作战选 1"),
                ("独立军路线", "破晓作战选 2"), ("罗姆斐拉／月球", "多鲁基斯被毁二选一"), ("独立军", "梁山泊之战"),
                ("完全和平路线", "重返战场时二选一"), ("OZ 后半", "乞力马扎罗的风暴起"), ("独立军后半", "银河帝国军先遣舰队起"),
                ("终章", "穆格宇宙／地球圈分头后汇合")],
    },
    "ja": {
        "switch": "日本語",
        "title": "スーパーロボット大戦64 攻略：フローチャート・隠し要素・データ", "nav": "攻略ページ",
        "flow": "フローチャート", "hidden": "隠し要素",
        "flow_lead": "話数順に並べ、同じ話数で横に並ぶカードは主人公やルートごとの別シナリオです。話数はゲームスクリプトのシナリオ接続から求め、各ルートで最も多い値を採っています。進み方によって実際の話数は前後するため、その差はカードの最初の備考に書いています。",
        "hidden_lead": "隠しキャラクター・機体・武器とルート分岐の条件です。本プロジェクトによるゲームスクリプトの解析を基準とし、海外攻略サイト Akurasu との違いを項目ごとに示しています。",
        "legend": ["加入", "スポット参戦", "離脱", "強化", "パーツ", "隠し要素"],
        "tags": {"diff": "Akurasu と相違", "new": "Akurasu 未記載", "unverified": "実機未確認"},
        "route_map": "ルート概要", "victory": "勝利", "defeat": "敗北", "after": "クリア後：",
        "b_join": "加入・入手", "b_temp": "スポット参戦", "b_leave": "離脱・使用不可", "b_upgrade": "強化・乗り換え・新武器",
        "b_parts": "強化パーツ（撃墜で入手）", "b_secret": "隠し要素と分岐", "b_notes": "備考",
        "stage": ("第", "話"), "step": "第{}話", "all": "すべて", "akurasu_box": "Akurasu との違い",
        "sources": "出典：", "sep": "／", "paren": "（{}）",
        "map": [("序盤", "主人公4人・リアル／スーパー系"), ("共通ルート", "オペレーション・デイブレイクまで"), ("OZルート", "デイブレイクで選択1"),
                ("独立軍ルート", "デイブレイクで選択2"), ("ロームフェラ／月", "トールギス破壊で二択"), ("独立軍", "梁山泊の戦い"),
                ("完全平和ルート", "戦場に帰るで二択"), ("OZ後半", "キリマンジャロの嵐から"), ("独立軍後半", "銀河帝国軍先遣艦隊から"),
                ("最終章", "ムゲ宇宙／地球圏に分かれて合流")],
    },
    "en": {
        "switch": "English",
        "title": "Super Robot Wars 64 Guide: Flow Chart, Secrets and Reference", "nav": "Guide pages",
        "flow": "Flow Chart", "hidden": "Secrets",
        "flow_lead": "Stages in order; cards side by side under one number are the scenarios of different protagonists or routes. Numbers are worked out from how the game's scripts chain the stages, using the most common value across routes; where your path gives a different number, the card's first note says so.",
        "hidden_lead": "Requirements for hidden pilots, units, weapons and route splits. They follow this project's reading of the game's stage scripts; every point where Akurasu says otherwise is marked.",
        "legend": ["Joins", "Guest", "Leaves", "Upgrade", "Parts", "Secrets"],
        "tags": {"diff": "Differs from Akurasu", "new": "Not on Akurasu", "unverified": "Not verified in game"},
        "route_map": "Routes at a glance", "victory": "Win", "defeat": "Lose", "after": "After the stage: ",
        "b_join": "Joins / obtained", "b_temp": "Guest units", "b_leave": "Leaves / unavailable", "b_upgrade": "Upgrades, new units, new weapons",
        "b_parts": "Parts (dropped when shot down)", "b_secret": "Secrets and splits", "b_notes": "Notes",
        "stage": ("Stage", ""), "step": "Stage {}", "all": "All", "akurasu_box": "Differences from Akurasu",
        "sources": "Sources: ", "sep": "; ", "paren": " ({})",
        "map": [("Opening", "Four leads · Real / Super"), ("Common route", "Up to Operation Daybreak"), ("OZ route", "Daybreak: choice 1"),
                ("Independence Army", "Daybreak: choice 2"), ("Romefeller / Moon", "Tallgeese Destroyed: pick one"), ("Independence Army", "Battle of Ryozanpaku"),
                ("Complete Pacifism", "Back to the Battlefield: pick one"), ("OZ, second half", "From Kilimanjaro"), ("IA, second half", "From the Galactic Vanguard"),
                ("Finale", "Split: Muge space / Earth, then rejoin")],
    },
}

MAP_BOXES = [("rm-box", 10, 95, 140), ("rm-box", 190, 95, 140), ("rm-oz", 376, 25, 124), ("rm-ia", 376, 130, 124),
             ("rm-oz", 536, 25, 124), ("rm-ia", 536, 130, 124), ("rm-cp", 536, 200, 128), ("rm-oz", 702, 35, 124),
             ("rm-ia", 702, 130, 124), ("rm-end", 866, 95, 124)]
MAP_EDGES = ["M150,125 L185,125", "M330,125 C350,125 350,55 372,55", "M330,125 C350,125 350,160 372,160",
             "M500,170 C515,170 515,228 532,228", "M500,160 L532,160", "M664,236 C684,236 684,180 698,175",
             "M664,220 C690,220 690,85 698,80", "M500,55 L532,55", "M660,55 L698,60", "M660,160 L698,160",
             "M826,65 C845,65 845,120 862,120", "M826,160 C845,160 845,130 862,130"]


def route_map(ui: dict) -> str:
    out = [f'<svg viewBox="0 0 1000 270" role="img" aria-labelledby="rm-title"><title id="rm-title">{esc(ui["route_map"])}</title>',
           '<defs><marker id="rm-arrow" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto">'
           '<path d="M0,0 L8,4 L0,8 z" fill="currentColor" style="color:var(--dot)"/></marker></defs>',
           '<g class="rm-edges" marker-end="url(#rm-arrow)">']
    out += [f'<path class="rm-edge" d="{d}"/>' for d in MAP_EDGES]
    out.append("</g>")
    for (cls, x, y, w), (title, sub) in zip(MAP_BOXES, ui["map"]):
        cx = x + w // 2
        out.append(f'<rect class="{cls}" x="{x}" y="{y}" width="{w}" height="60" rx="9"/>'
                   f'<text x="{cx}" y="{y + 26}" text-anchor="middle">{esc(title)}</text>'
                   f'<text class="rm-sub" x="{cx}" y="{y + 44}" text-anchor="middle">{esc(sub)}</text>')
    out.append("</svg>")
    return "".join(out)


def esc(value) -> str:
    return html.escape(str(value), quote=True)


def load(lang: str, name: str):
    return json.loads((DATA / lang / name).read_text(encoding="utf-8"))


ROUTE_CLASS = {"oz": "lane oz", "cp": "lane cp", "common": "lane common", "super": "lane super"}
TAGS = {"{≠}": "diff", "{+}": "new", "{?}": "unverified"}


class Page:
    def __init__(self, lang: str):
        self.lang = lang
        self.ui = UI[lang]

    def inline(self, text) -> str:
        """Escape text and turn the {≠} {+} {?} markers into badges at the end."""
        text = str(text)
        found = [kind for mark, kind in TAGS.items() if mark in text]
        for mark in TAGS:
            text = text.replace(mark, "")
        return esc(text.strip()) + "".join(f'<span class="tag {k}">{esc(self.ui["tags"][k])}</span>' for k in found)

    def items(self, values) -> str:
        return "".join(f"<li>{self.inline(v)}</li>" for v in values)

    def block(self, cls: str, title: str, body: str, list_cls: str = "info-list") -> str:
        return (f'<section class="stage-block {cls}"><h4><span class="block-dot"></span>{esc(title)}</h4>'
                f'<ul class="{list_cls}">{body}</ul></section>')

    def card(self, card: dict, secrets: dict) -> str:
        ui = self.ui
        out = [f'<article class="stage-card" id="{esc(card["id"])}">',
               f'<header class="stage-header"><span class="{ROUTE_CLASS.get(card.get("route"), "lane")}">{esc(card["lane"])}</span>',
               f'<h3>{esc(card["title"])}</h3>']
        if card.get("title_ja") and card["title_ja"] != card["title"]:
            out.append(f'<p class="source-title" lang="ja">{esc(card["title_ja"])}</p>')
        cond = []
        if card.get("victory"):
            cond.append(f"<dt>{esc(ui['victory'])}</dt><dd>{self.inline(card['victory'])}</dd>")
        if card.get("defeat"):
            cond.append(f"<dt>{esc(ui['defeat'])}</dt><dd>{self.inline(card['defeat'])}</dd>")
        if cond:
            out.append(f'<dl class="conditions">{"".join(cond)}</dl>')
        out.append("</header>")
        blocks = []
        joins, after = card.get("joins") or [], card.get("joins_after") or []
        if joins or after:
            body = self.items(joins) + "".join(f"<li>{esc(ui['after'])}{self.inline(v)}</li>" for v in after)
            blocks.append(self.block("acquisition", ui["b_join"], body))
        for key, cls, title in (("temporary", "temporary", "b_temp"), ("leaves", "availability", "b_leave"),
                                ("upgrades", "upgrade", "b_upgrade")):
            if card.get(key):
                blocks.append(self.block(cls, ui[title], self.items(card[key])))
        if card.get("parts"):
            body = "".join(
                f'<li>{esc(p["part"])} <span class="part-from">← {self.inline(p["from"])}</span>'
                + (f' <span class="part-note">{esc(ui["paren"].format(p["note"]))}</span>' if p.get("note") else "")
                + "</li>" for p in card["parts"])
            blocks.append(self.block("parts", ui["b_parts"], body))
        if card.get("secrets"):
            body = "".join(
                f'<li><a href="#{esc(s["ref"])}">{esc(secrets.get(s["ref"], {}).get("title", s["ref"]))}</a>'
                f'<p>{self.inline(s["text"])}</p></li>' for s in card["secrets"])
            blocks.append(self.block("hidden-progress", ui["b_secret"], body, "hidden-list"))
        if card.get("notes"):
            blocks.append(self.block("notes", ui["b_notes"], self.items(card["notes"])))
        if blocks:
            out.append(f'<div class="stage-content">{"".join(blocks)}</div>')
        out.append("</article>")
        return "".join(out)

    def tag_legend(self) -> str:
        return '<ul class="tag-legend">' + "".join(
            f'<li><span class="tag {k}">{esc(v)}</span></li>' for k, v in self.ui["tags"].items()) + "</ul>"

    def flow(self, progression: dict, secrets: dict) -> str:
        ui = self.ui
        colors = ["--blue", "--violet", "--orange", "--green", "--teal", "--amber"]
        legend = "".join(f'<li><span class="block-dot" style="background:var({c})"></span>{esc(t)}</li>'
                         for c, t in zip(colors, ui["legend"]))
        out = ['<section class="guide-panel" id="flow" data-panel-content="flow">',
               f'<div class="page-head"><div><h1>{esc(ui["flow"])}</h1><p>{esc(ui["flow_lead"])}</p>{self.tag_legend()}</div>'
               f'<ul class="legend">{legend}</ul></div>',
               '<div class="flow-intro">',
               f'<section class="intro-card route-map"><h2>{esc(ui["route_map"])}</h2>{route_map(ui)}</section>']
        for box in (progression.get("intro") or {}).get("boxes", []):
            out.append(f'<section class="intro-card"><h2>{esc(box["title"])}</h2><ul>{self.items(box["items"])}</ul></section>')
        out.append("</div>")
        sections: list[tuple[str, list]] = []
        for card in progression["cards"]:
            if not sections or sections[-1][0] != card["section"]:
                sections.append((card["section"], []))
            sections[-1][1].append(card)
        before, after = ui["stage"]
        for n, (name, cards) in enumerate(sections, 1):
            out.append(f'<section class="flow-section" id="flow-{n}"><h2>{esc(name)}</h2>')
            rows: dict[str, list] = {}
            for card in cards:
                rows.setdefault(card["stage"], []).append(card)
            for stage, row in rows.items():
                many = " many" if len(row) >= 4 else ""
                out.append(f'<div class="flow-row{many}"><div class="stage-number"><span>{esc(before)}</span>'
                           f'<strong>{esc(stage)}</strong><span>{esc(after)}</span></div><div class="stage-lanes">')
                out.extend(self.card(c, secrets) for c in row)
                out.append("</div></div>")
            out.append("</section>")
        out.append("</section>")
        return "".join(out)

    def hidden(self, hidden: dict) -> str:
        ui = self.ui
        out = ['<section class="guide-panel" id="hidden-elements" data-panel-content="hidden-elements" hidden>',
               f'<div class="page-head"><div><h1>{esc(ui["hidden"])}</h1><p>{esc(ui["hidden_lead"])}</p>{self.tag_legend()}</div></div>',
               f'<div class="category-filters"><button class="active" data-category="all">{esc(ui["all"])}</button>']
        labels = {}
        for cat in hidden["categories"]:
            labels[cat["id"]] = cat["label"]
            out.append(f'<button data-category="{esc(cat["id"])}">{esc(cat["label"])}</button>')
        out.append("</div>")
        if hidden.get("intro"):
            intro = hidden["intro"]
            out.append(f'<section class="intro-card hidden-intro"><h2>{esc(intro["title"])}</h2>'
                       f'<ul>{self.items(intro["items"])}</ul></section>')
        out.append('<div class="secret-grid">')
        for e in hidden["entries"]:
            out.append(f'<article class="secret-card" id="{esc(e["id"])}" data-category="{esc(e["category"])}">')
            out.append(f'<div><span class="secret-kicker">{esc(e.get("kicker") or labels[e["category"]])}</span>')
            if e.get("applies"):
                out.append(f'<span class="secret-applies">{esc(e["applies"])}</span>')
            out.append(f'</div><h3>{esc(e["title"])}</h3>')
            if e.get("summary"):
                out.append(f'<p class="secret-summary">{self.inline(e["summary"])}</p>')
            if e.get("steps"):
                out.append('<ol class="secret-steps">')
                for s in e["steps"]:
                    stage = s.get("stage") or ""
                    label = ui["step"].format(stage) if any(ch.isdigit() for ch in stage) else stage
                    head = f'<span class="when">{esc(label)}</span>' if stage else ""
                    if s.get("stage_title"):
                        head += f'<span class="step-stage">{esc(s["stage_title"])}</span>'
                    if s.get("route"):
                        head += f'<span class="step-route">{esc(s["route"])}</span>'
                    out.append(f'<li><div class="step-head">{head}</div><p>{self.inline(s["text"])}</p></li>')
                out.append("</ol>")
            if e.get("result"):
                out.append(f'<p class="secret-result">{self.inline(e["result"])}</p>')
            if e.get("notes"):
                out.append(f'<ul class="secret-notes">{self.items(e["notes"])}</ul>')
            if e.get("akurasu_notes"):
                out.append(f'<div class="correction"><p class="correction-title">{esc(ui["akurasu_box"])}</p>'
                           f'<ul>{self.items(e["akurasu_notes"])}</ul></div>')
            out.append("</article>")
        out.append("</div></section>")
        return "".join(out)

    def tab(self, tab: dict) -> str:
        out = [f'<section class="guide-panel" id="{esc(tab["id"])}" data-panel-content="{esc(tab["id"])}" hidden>',
               f'<div class="page-head"><div><h1>{esc(tab["label"])}</h1>']
        if tab.get("intro"):
            out.append(f'<p>{self.inline(tab["intro"])}</p>')
        out.append('</div></div><div class="reference-shell">')
        for b in tab["blocks"]:
            out.append('<section class="reference-card">')
            if b.get("title"):
                out.append(f'<h2>{esc(b["title"])}</h2>')
            if b.get("lead"):
                out.append(f'<p class="reference-lead">{self.inline(b["lead"])}</p>')
            if b["type"] == "table":
                head = "".join(f"<th>{esc(c)}</th>" for c in b["columns"])
                body = "".join(
                    "<tr>" + "".join(
                        f'<td class="num">{self.inline(c)}</td>' if is_number(c) else f"<td>{self.inline(c)}</td>"
                        for c in row) + "</tr>" for row in b["rows"])
                out.append(f'<div class="table-wrap"><table class="reference-table"><thead><tr>{head}</tr></thead>'
                           f"<tbody>{body}</tbody></table></div>")
            elif b["type"] == "list":
                out.append(f'<ul class="note-list list-block">{self.items(b["items"])}</ul>')
            if b.get("notes"):
                out.append(f'<ul class="note-list">{self.items(b["notes"])}</ul>')
            out.append("</section>")
        out.append("</div></section>")
        return "".join(out)

    def root(self, langs: list[str]) -> str:
        """This language's tab bar and panels, with element ids prefixed by the language."""
        ui = self.ui
        progression = load(self.lang, "progression.json")
        hidden = load(self.lang, "hidden-elements.json")
        reference = load(self.lang, "reference.json")
        secrets = {e["id"]: e for e in hidden["entries"]}
        tabs = [("flow", ui["flow"]), ("hidden-elements", ui["hidden"])] + [(t["id"], t["label"]) for t in reference["tabs"]]
        nav = "".join(
            f'<a class="mode-tab{" active" if i == 0 else ""}" data-panel="{esc(pid)}" href="#{esc(pid)}">{esc(label)}</a>'
            for i, (pid, label) in enumerate(tabs))
        switch = ""
        if len(langs) > 1:
            switch = '<div class="lang-switch">' + "".join(
                f'<a lang="{l}" data-lang="{l}" href="?lang={l}">{esc(UI[l]["switch"])}</a>' for l in langs) + "</div>"
        body = self.flow(progression, secrets) + self.hidden(hidden) + "".join(self.tab(t) for t in reference["tabs"])
        sources = progression.get("sources") or []
        foot = (f'<footer class="site-foot">{esc(ui["sources"])}' + esc(ui["sep"]).join(esc(s) for s in sources)
                + "</footer>") if sources else ""
        inner = f'<nav class="mode-tabs" aria-label="{esc(ui["nav"])}">{nav}{switch}</nav>\n<main>{body}{foot}</main>'
        prefix = self.lang + "--"
        inner = re.sub(r' id="([^"]+)"', lambda m: f' id="{prefix}{m.group(1)}"', inner)
        inner = inner.replace('aria-labelledby="rm-title"', f'aria-labelledby="{prefix}rm-title"')
        inner = inner.replace("url(#rm-arrow)", f"url(#{prefix}rm-arrow)")
        return (f'<div class="lang-root" lang="{self.lang}" data-lang="{self.lang}" data-title="{esc(ui["title"])}"'
                f'{"" if self.lang == langs[0] else " hidden"}>\n{inner}\n</div>\n')


def render_all(langs: list[str]) -> str:
    """One self-contained page holding every language; the script shows one at a time."""
    first = UI[langs[0]]
    roots = "".join(Page(l).root(langs) for l in langs)
    return (f"<!doctype html>\n<html lang=\"{langs[0]}\">\n<head>\n<meta charset=\"utf-8\">\n"
            "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n<link rel=\"icon\" href=\"data:,\">\n"
            f"<title>{esc(first['title'])}</title>\n<style>{CSS}</style>\n</head>\n<body>\n{roots}"
            f"<script>{SCRIPT}</script>\n</body></html>\n")


def is_number(cell) -> bool:
    s = str(cell)
    for mark in TAGS:
        s = s.replace(mark, "")
    s = s.replace(",", "").replace("+", "").replace("%", "").replace("−", "-").strip()
    try:
        float(s)
        return bool(s)
    except ValueError:
        return False


def languages() -> list[str]:
    return [l for l in LANGS if (DATA / l / "progression.json").exists()]


OUT = GUIDE / "srw64-flow-guide.html"


def pages() -> dict[Path, str]:
    return {OUT: render_all(languages())}


def check_data(lang: str) -> list[str]:
    """Every link resolves, and a translation has exactly the reference language's ids."""
    errors = []
    progression, hidden, reference = (load(lang, n) for n in ("progression.json", "hidden-elements.json", "reference.json"))
    ids = {e["id"] for e in hidden["entries"]}
    for card in progression["cards"]:
        for s in card.get("secrets") or []:
            if s["ref"] not in ids:
                errors.append(f'{lang} {card["id"]}: unknown hidden element {s["ref"]}')
    if lang != LANGS[0]:
        base = [load(LANGS[0], n) for n in ("progression.json", "hidden-elements.json", "reference.json")]
        for name, mine, theirs in (("cards", progression["cards"], base[0]["cards"]),
                                   ("entries", hidden["entries"], base[1]["entries"]),
                                   ("tabs", reference["tabs"], base[2]["tabs"])):
            if [x["id"] for x in mine] != [x["id"] for x in theirs]:
                errors.append(f"{lang}: {name} ids differ from {LANGS[0]}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail when a page differs from the data")
    args = parser.parse_args()
    errors = [e for l in languages() for e in check_data(l)]
    for e in errors:
        print(e, file=sys.stderr)
    if errors:
        return 1
    stale = 0
    for path, page in pages().items():
        if args.check:
            if not path.exists() or path.read_text(encoding="utf-8") != page:
                print(f"{path.relative_to(ROOT)} is stale; run tools/content/build_guide.py", file=sys.stderr)
                stale = 1
            continue
        path.write_text(page, encoding="utf-8")
        print(f"wrote {path.relative_to(ROOT)} ({len(page.encode()) // 1024} KiB)")
    return stale


if __name__ == "__main__":
    sys.exit(main())
