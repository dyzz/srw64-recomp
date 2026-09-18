"""Publish the local stage-one experiment beside the historical model gallery."""
from pathlib import Path
import argparse, hashlib, html, json, shutil
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
DEST = ROOT / 'assets/hd-ai/2026-09-08/gallery'

def comparison_sheet(original, native, output):
    """Lay out real screenshots without redrawing or retouching game content."""
    left=Image.open(original).convert('RGB')
    right=Image.open(native).convert('RGB')
    if left.size!=(640,480) or right.size!=(960,720):
        raise ValueError('expected the verified 4:3 Japanese and native captures')
    left=left.resize(right.size,Image.Resampling.NEAREST)
    sheet=Image.new('RGB',(1968,880),'#101719')
    font_dir=ROOT/'assets/hd-ai/dialogue-polish/fonts'
    title=ImageFont.truetype(str(font_dir/'HarmonyOS_Sans_SC_Medium.ttf'),30)
    small=ImageFont.truetype(str(font_dir/'HarmonyOS_Sans_SC_Regular.ttf'),20)
    draw=ImageDraw.Draw(sheet)
    panels=[(16,left,'日文原版 · 模拟器','Mupen64Plus-Next / Angrylion · 原版字体、头像和地图'),
            (992,right,'中文高清 · Native recomp','RT64 / Metal · HarmonyOS Medium + AI 头像 · 内部 4 倍')]
    for x,picture,label,detail in panels:
        draw.text((x,16),label,font=title,fill='#e7eee9')
        draw.text((x,60),detail,font=small,fill='#b4c2bb')
        sheet.paste(picture,(x,104))
        # In particular, keep every pixel of the native capture unchanged.
        if sheet.crop((x,104,x+960,824)).tobytes()!=picture.tobytes():
            raise ValueError('comparison layout changed screenshot pixels')
    draw.text((16,844),'女主线第一话 · 同一段过场对白 · 两侧均为实际运行截图，统一按 4:3 显示',font=small,fill='#b4c2bb')
    output.parent.mkdir(parents=True,exist_ok=True)
    sheet.save(output)
    return output

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pack', type=Path, required=True)
    ap.add_argument('--portraits', type=Path, required=True)
    ap.add_argument('--before-frame', type=Path, help='real GPU capture paired with the first frame')
    ap.add_argument('--frame', action='append', default=[], help='caption=path to a real GPU capture')
    ap.add_argument('--resolution-frame', action='append', default=[], help='label=path to a same-task GPU replay at a given internal scale')
    ap.add_argument('--emulator-reference', type=Path, help='verified Japanese original-ROM reference manifest, paired with the first native frame')
    args = ap.parse_args()
    media = DEST / 'stage1-media'
    media.mkdir(parents=True, exist_ok=True)
    hashes = {}
    def asset(path, name):
        target = media / name
        shutil.copy2(path, target)
        hashes[name] = hashlib.sha256(target.read_bytes()).hexdigest()
        return 'stage1-media/' + name
    def compare(title, left, right, note, pixel=True, left_label='原始', right_label='处理后'):
        return f'''<article><h2>{html.escape(title)}</h2><div class="compare">
        <img class="original {'pixel' if pixel else ''}" src="{left}" alt="原始图像">
        <div class="after"><img src="{right}" alt="处理后图像"></div><div class="line"></div>
        <span class="tag left">{html.escape(left_label)}</span><span class="tag right">{html.escape(right_label)}</span>
        <input aria-label="{html.escape(title)} 对比位置" type="range" min="0" max="100" value="50" oninput="this.parentElement.style.setProperty('--split',this.value+'%')"></div>
        <p>{html.escape(note)}</p></article>'''
    cards = []
    for s in json.loads((args.portraits / 'samples.json').read_text())['samples']:
        rid = s['resource_id']
        before = asset(args.portraits / s['source'], f'portrait-{rid}-source.png')
        after = asset(args.pack / f'portrait-{rid}-384.png', f'portrait-{rid}-384.png')
        note = '复用已选中的 Qwen 3.0 候选 2，游戏使用 384×384。' if rid == 29 else 'Qwen 3.0 新输出 512×512，游戏使用 384×384。'
        cards.append(compare(s['character'], before, after, note))
    maps = []
    for rid, title in [(5604,'欧洲过场地图'), (5605,'亚洲地图图层')]:
        before = asset(args.pack / f'map-{rid}-source.png', f'map-{rid}-source.png')
        after = asset(args.pack / f'map-{rid}-lanczos4.png', f'map-{rid}-lanczos4.png')
        maps.append(compare(title, before, after, '本地 Lanczos 4 倍：整层放大后切回纹理块，保留透明通道。拖动滑块查看边缘变化。'))
    frames = []
    reference = ''
    for i, spec in enumerate(args.frame):
        caption, path = spec.split('=', 1)
        src = asset(Path(path), f'runtime-{i}.png')
        if i == 0 and args.emulator_reference:
            record=json.loads(args.emulator_reference.read_text())
            if record['status']!='verified' or record['rom']['sha256']!='ee5f4a21d8e5f7827d21199e27800edf250df9a8e4d10befb1a6992df597b13e':
                raise ValueError('reference must be a verified original Japanese ROM capture')
            for kind in ('raw','display'):
                item=record[kind]
                if hashlib.sha256((ROOT/item['path']).read_bytes()).hexdigest()!=item['sha256']:
                    raise ValueError('emulator reference image differs from its manifest')
            raw=asset(ROOT/record['raw']['path'],'jp-emulator-raw.png')
            original=asset(ROOT/record['display']['path'],'jp-emulator-4by3.png')
            provenance=asset(args.emulator_reference,'jp-emulator-reference.json')
            composite=comparison_sheet(ROOT/record['display']['path'],Path(path),args.emulator_reference.parent/'jp-vs-hd-dialogue.png')
            combined=asset(composite,'jp-vs-hd-dialogue.png')
            note='日文原始 ROM 在 Mupen64Plus-Next + Angrylion 中运行，保留原字体、头像和地图；右侧为当前中文高清版。同一段对白：上方劳伦斯说明情报工作的重要性，下方保留玛娜米上一句对白。原始输出 640×240，展示图仅将扫描行重复为 640×480，恢复 4:3 显示比例。'
            reference=f'<section id="jp-reference"><h2>过场对话 · 日文原版模拟器 / 中文高清</h2><figure><a href="{combined}" target="_blank"><img src="{combined}" alt="同一段过场：左侧日文原版模拟器，右侧中文高清 native recomp"></a><figcaption>一张图并排对照；点击查看大图。日文画面仅按最近邻放大到同等显示尺寸，高清画面保持原截图像素。</figcaption></figure><p>{html.escape(note)}</p><p><a href="{combined}" download>下载完整对比图</a> · <a href="{original}" target="_blank">日文 4:3 截图</a> · <a href="{raw}" target="_blank">原始模拟器输出</a> · <a href="{provenance}" target="_blank">来源记录</a></p></section>'
        if i == 0 and args.before_frame:
            before = asset(args.before_frame, 'runtime-before.png')
            frames.append('<div class=dialogue-compare>' + compare('过场对话 · 修改前 / 修改后', before, src, caption, pixel=False) + '</div>')
            continue
        frames.append(f'<figure><img src="{src}" alt="{html.escape(caption)}"><figcaption>{html.escape(caption)}</figcaption></figure>')
    resolution = ''
    if args.resolution_frame:
        choices=[]
        for i,spec in enumerate(args.resolution_frame):
            label,path=spec.split('=',1)
            src=asset(Path(path),f'resolution-{i}.png')
            choices.append((label,src))
        # Keep every image at the same CSS size: this compares render precision,
        # not differently enlarged text. These are renderer replays, not game runs.
        buttons=''.join(f'<button type="button" data-src="{src}" aria-pressed="{str(i==0).lower()}">{html.escape(label)}</button>' for i,(label,src) in enumerate(choices))
        label,src=choices[0]
        resolution=f'''<section class="section"><h2>内部渲染分辨率</h2><p>切换同一过场、同一字体和素材的 GPU 渲染回放。图片始终按相同大小展示，文字逻辑大小不变。此处是固定任务回放，画面亮度与上方实时运行截图有差异；仅用于各倍率之间的对照。</p><div class="resolution-picker" role="group" aria-label="内部渲染倍率">{buttons}</div><figure class="resolution-view"><img id="resolution-image" src="{src}" alt="{html.escape(label)}的同帧渲染"><figcaption id="resolution-caption">{html.escape(label)}</figcaption></figure><p>试玩默认 4 倍，可用 <code>--resolution-scale 1..8</code> 调整。显示窗口和内部渲染精度相互独立；更高倍率不会增加原始地图素材没有的内容。</p></section><script>document.querySelectorAll('.resolution-picker button').forEach(button=>button.addEventListener('click',()=>{{document.querySelectorAll('.resolution-picker button').forEach(b=>b.setAttribute('aria-pressed',String(b===button)));const img=document.getElementById('resolution-image');img.src=button.dataset.src;img.alt=button.textContent+'的同帧渲染';document.getElementById('resolution-caption').textContent=button.textContent;}}));</script>'''
    page = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>女主线第一话 · SRW64 高清测试</title>
<style>
:root{color-scheme:dark;font-family:system-ui,-apple-system,sans-serif;background:#101719;color:#e7eee9}*{box-sizing:border-box}body{margin:0}main{max-width:1240px;margin:auto;padding:32px 28px 60px}a{color:#c3dc98;text-decoration:none}header{border-bottom:1px solid #33403f;display:flex;justify-content:space-between;padding-bottom:22px;font-size:14px}h1{font-weight:550;font-size:clamp(28px,4vw,45px);margin:44px 0 14px;letter-spacing:-1px}h2{font-size:18px;font-weight:550;margin:20px 0 14px}p{color:#a6b6af;font-size:14px;line-height:1.8;max-width:880px}.eyebrow{color:#c3dc98;font-size:12px;letter-spacing:2px;margin-top:40px}.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:20px}.maps{display:grid;grid-template-columns:1fr 1fr;gap:24px}.compare{position:relative;aspect-ratio:1;overflow:hidden;background:repeating-conic-gradient(#333d40 0% 25%,#414d50 0% 50%) 50%/24px 24px;--split:50%;border-radius:12px;border:1px solid #48524d}.compare img{width:100%;height:100%;object-fit:contain;display:block}.compare .original{position:absolute;inset:0}.pixel{image-rendering:pixelated}.after{position:absolute;inset:0;clip-path:inset(0 0 0 var(--split))}.line{position:absolute;top:0;bottom:0;left:var(--split);width:2px;background:#d4e5ba;pointer-events:none}.compare input{position:absolute;inset:0;opacity:0;width:100%;height:100%;margin:0;cursor:ew-resize}.tag{position:absolute;top:12px;padding:4px 9px;background:#18221ddb;border-radius:4px;font-size:11px}.left{left:10px}.right{right:10px}.maps .compare{aspect-ratio:1.35}.maps .compare img{object-fit:contain}figure{margin:24px 0;border:1px solid #33403f;border-radius:12px;overflow:hidden;background:#172023}figure img{width:100%;display:block}figcaption{padding:16px 20px;color:#c0cec5;font-size:14px}.runtime{display:grid;grid-template-columns:1fr 1fr;gap:20px}.dialogue-compare{grid-column:1/-1;width:100%;max-width:960px;margin:auto}.dialogue-compare .compare{aspect-ratio:4/3}.runtime>figure{grid-column:1/-1;max-width:720px;margin:24px auto;width:100%}.notice{background:#1e2a25;border-left:3px solid #accd85;padding:15px 20px;margin:28px 0}.section{border-top:1px solid #33403f;margin-top:45px;padding-top:16px}footer{margin-top:45px;color:#8e9d96;font-size:12px;line-height:1.8}@media(max-width:800px){main{padding:24px 18px}.grid{grid-template-columns:1fr 1fr}.maps,.runtime{grid-template-columns:1fr}.runtime figure:first-child{grid-column:auto}header{gap:20px;font-size:12px}}
</style><main><header><a href="/">← 图像实验室 / 历史模型对照</a><span>本地测试 · 2026.09.08</span></header><div class="eyebrow">STAGE 01 / MANAMI</div><h1>中文文字、AI 头像与本地地图</h1><p>测试范围收敛到女主线第一话。文字使用 HarmonyOS Sans SC Medium，保持原字号，修正标点亮度，并保留统一基线和对话框留白；头像采用 Qwen 3.0；过场地图使用本地 Lanczos 4 倍处理。</p><div class="notice">机械单位图标、战术地图 tile 和 UI 边框暂不处理。地图的本地放大改善边缘，保留原画内容与细节量。</div>
<section><h2>实时游戏画面</h2><p>以下均来自本轮 native recomp + RT64 / Metal 的 GPU 截图；本次只验证过场对话。它们证明对应画面，不代表整关所有分支已验收。</p><div class="runtime">__FRAMES__</div></section><section class="section"><h2>人物头像</h2><p>原图 96×96 → 游戏纹理 384×384。按 AI 头像自身轮廓去除灰底、清理边缘颜色，再切回游戏使用的 9 个纹理块。拖动图片中的滑块比较。</p><div class="grid">__CARDS__</div></section><section class="section"><h2>过场地图 · 本地 4 倍</h2><p>按透明度预乘后整层缩放，再按原布局切块。3 个相同源哈希在不同邻接位置产生冲突，当前保留原图，避免替换到错误内容。</p><div class="maps">__MAPS__</div></section><footer>中文测试稿覆盖 153 条菜单、姓名与第一话开场至战后对白；未覆盖的条目保留日文。后续登场角色仍可能使用原头像。<br>中文姓名仍保留游戏使用的原始分隔符编码；本轮姓名确认崩溃已添加构建防护。</footer></main></html>'''
    page = page.replace('__FRAMES__', ''.join(frames)).replace('__CARDS__',''.join(cards)).replace('__MAPS__',''.join(maps))
    page = page.replace('<section><h2>实时游戏画面</h2>',reference+'<section><h2>实时游戏画面</h2>')
    page = page.replace('<section class="section"><h2>人物头像</h2>',resolution+'<section class="section"><h2>人物头像</h2>')
    page = page.replace('</style>', '.resolution-picker{display:flex;gap:10px;flex-wrap:wrap;margin-top:20px}.resolution-picker button{font:inherit;color:#c0cec5;background:#172023;border:1px solid #48524d;border-radius:7px;padding:10px 16px;cursor:pointer}.resolution-picker button[aria-pressed=true]{background:#c3dc98;color:#172023;border-color:#c3dc98}.resolution-view{max-width:960px;margin:18px auto}code{font-size:13px}</style>')
    (DEST / 'stage1.html').write_text(page)
    (DEST / 'stage1-manifest.json').write_text(json.dumps({'schema':'srw64.stage1-gallery.v1','pack':str(args.pack.resolve()),'emulator_reference':str(args.emulator_reference.resolve()) if args.emulator_reference else None,'files':hashes},indent=2)+'\n')
    print(DEST / 'stage1.html')

if __name__ == '__main__':
    main()
