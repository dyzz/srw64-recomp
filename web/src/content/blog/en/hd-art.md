---
title: How the HD art was made
date: 2026-10-05
summary: "Portraits, backgrounds, maps, units, icons, battle scenes, frames and the title, category by category: where each kind of image came from, how it was made, and how it gets back into the game."
---

The HD art pack is optional. Without it you get the original graphics, and with it <kbd>F6</kbd> switches back to the original at any time. This post goes through the pack one kind of image at a time. In every comparison below, the original (enlarged pixel for pixel) is on the left and HD is on the right.

A few rules run through all of it:

- **The original sets the composition.** Whether a picture was redrawn by AI or upscaled locally, it is aligned back to the original pixel by pixel: outline, position and size follow the original, and nothing grows that the original does not have.
- **Gameplay is untouched.** HD only changes pictures. Game logic, saves and rules stay the same, and you can switch between original and HD at any time.
- **What can be computed from your ROM stays out of the pack.** Window frames, the BANPRESTO logo and GAME OVER are generated at run time from your own ROM, so the HD pack carries none of that original data.

## Four ways back into the game

A finished picture reaches the screen in one of four ways:

| Path | How | Used for |
| --- | --- | --- |
| Whole-image drawing | The game still draws the picture as small tiles; the host recognises which picture it is, blanks the tiles and draws one HD image at the same moment and the same depth | Portraits, intermission backgrounds, tactical maps, the title, unit and battle sprites |
| Texture replacement | A small texture is swapped by its RT64 renderer hash. The hashes can be computed offline from the ROM, without running the game | World map surfaces, space objects, dialogue and HUD frames, map unit icons, battle ground |
| Native models | The original 3D display list is recognised, skipped, and our own model is drawn instead | Ships, landmarks and the location marker on the world map |
| Native text | Text that the original painted into images is typeset again in the current language | Title menu, chapter cards, prologue, ending, credits |

Whole-image drawing leaves no seams. The original's large pictures are dozens of small textures, and replacing them one by one would show at the joins.

## Character portraits

![Character portraits: original and HD](/media/hd-portraits.webp)

*Left: original, 96×96. Right: HD, 768×768.*

![More portraits: original and HD](/media/hd-portraits-grid.webp)

*Eight more, original left, HD right in each pair.*

- **Original**: 300 portraits of about 96×96, 64 colours each.
- **Generation**: Alibaba Cloud Qwen, qwen-image-3.0. Each request sent four portraits as a 2×2 grid (grey background, 6× pixel enlargement): 75 requests, about 15 yuan (around 2 US dollars). A green-screen background (the model enlarged every cell) and 3×3 grids (cells too small) both failed.
- **Alignment**: the model scaled cells by 3–10% and moved them by up to 7 pixels, so every cell is registered with a least-squares affine fit over 32-pixel blocks and resampled to an 8× master.
- **Matting**: the outline is held within ±1.5 original pixels of the original's. Where the model stopped short of the frame edge (130 of 300), the edge colour is carried out to it. The median overlap (IoU) with the original outline is 0.991.
- **A trap**: early mattes stored transparent pixels as black, and the renderer filters without premultiplied alpha, so outlines got a dark fringe. Filling transparent pixels with the nearest solid colour, and scaling colour and alpha separately, removed it.
- **Result**: 304 images at 768×768, whole-image drawing.

## Intermission backgrounds

![Intermission background: original and HD](/media/hd-background.webp)

*Top: the whole picture, original 320×240 on the left and HD widened to 16:9 on the right. Bottom: the same spot at 5×, original left, HD right.*

![More intermission backgrounds: original and HD](/media/hd-backgrounds-more.webp)

*Four more intermission backgrounds (the 4:3 part), original left, HD right in each pair.*

- **Original**: 8 images of 320×240, each with a bright and a dark palette.
- **Generation**: redrawn with Qwen qwen-image-3.0-pro and registered at 1920×1440. The dark versions are not generated. A 17³ colour lookup table fitted on the two ROM palettes derives them from the bright ones, off by only 1.2–2.6 levels (a plain ×0.75 is off by 3–23).
- **Widescreen**: later outpainted on both sides with Alibaba Cloud Wanx (wanx2.1-imageedit) to 2560×1440.
- **Result**: 16 images plus a starfield, whole-image drawing.

## Story world map

![Story world map: original and HD](/media/hd-worldmap.webp)

*Left: original Europe (the Mediterranean). Right: HD.*

![Central Asia: original and HD](/media/hd-worldmap-asia.webp)

*Central Asia (around the Caspian Sea), original left, HD right.*

![Space: original and HD](/media/hd-space.webp)

*The Earth and the Moon in the space region, original left, HD right in each pair.*

- **Original**: 64×64 4-bit surface tiles for the whole Earth, Europe, Central Asia, North America and space.
- **Generation**: several rounds with Qwen never looked right, so all four surfaces were redrawn with OpenAI's image model (through Codex). Each region was painted whole as a base, then in 3:2 windows for detail. When joining, pixels further from a window's inner edge weigh more, and coastlines are cut to the original land and sea.
- **Two traps**: a wrong row pitch in the tiling (600, not 611) left a seam, and a purple fringe along the coasts needed its own clean-up.
- **Close-ups**: when the story zooms to a location, one original pixel covers 19–30 screen pixels, so 19 frequently visited locations got their own close-up windows.
- **Result**: 213 textures of 512×512 (8× the original), texture replacement. Space has 27 textures from Qwen, and the starfield is drawn whole.

## Ships, landmarks and the marker on the world map

The ships flying over the story's world map, the landmarks in space and the yellow diamond that marks a location are all very simple 3D models in the original. HD replaces every one of them with a rebuilt model. Everything below is 0.3.5 in game: a screenshot in HD, then the same moment switched to the original.

![Ships (1): original and HD](/media/hd-fleet-1.webp)

*Original left, HD right in each pair. Left to right, top to bottom: Gran-Garan, Goraon, Gandor, La Vie en Rose, Barge, Libra.*

![Ships (2): original and HD](/media/hd-fleet-2.webp)

*Peacemillion, Medea, Audhumla, Albion, Argama, Nahel Argama, Ra Cailum.*

- **Ships**: 13 ships were rebuilt in Blender, each from a script and the official designs, with 8,000 to 21,000 faces (the originals have 4 to 330). Trails are now shader-drawn ribbons.
- **Name plates**: ships with a name plate, such as Libra and Barge, have it typeset again in the current language (these shots use the Japanese interface).

![Landmarks: original and HD](/media/hd-landmarks.webp)

*Top: Axis. Bottom: Fifth Luna and its name plate. Original left, HD right.*

- **Landmarks**: Axis and Fifth Luna were rebuilt the same way; with the 13 ships, 15 models in all.

![Location marker: original and HD](/media/hd-marker.webp)

*The yellow diamond that marks a location, original left, HD right.*

- **Location marker**: it keeps the original's double pyramid, with its edges rounded and a gold reflective material; the dashed ring around it is drawn by a shader.
- **Anti-aliasing**: these models are drawn with the rest of the scene at 4× multisampling (MSAA), so their edges are smooth.
- All of these are native models. The HD pack holds none of the original model data.
## Tactical maps

![Tactical map: original and HD](/media/hd-tactical.webp)

*The town on map 20. Left: original, enlarged pixel for pixel. Right: the HD base.*

![Original, HD base and index map](/media/hd-tactical-index.webp)

*Left to right: original, HD base, palette-index map (shown with the original palette). At run time the index map and the live palette tint the base, so water flows and night maps change colour.*

![Colony frames: original and HD](/media/hd-colony.webp)

*The space colony's 8-frame animation, original left, HD right.*

Tactical maps were the largest job: 158 records, 109 root maps and 22 variants, 131 maps in use.

- **Generation**: 4× the original is mostly beyond Qwen's 2048×2048 limit, so every base was painted with OpenAI's image model: 109 whole maps as bases, then 772 detail windows (each 384×256 original pixels, aligned to the grid and checked cell by cell), and 50 more windows for the variants. Over 900 pictures in all.
- **Water and light**: the original animates water and lights by cycling palette colours, and night battles swap the whole palette. Water painted into the base could never move. So water and borders stay out of the paintings, and a 4× **palette-index map** (original colour indices only, enlarged with MMPX) goes with each base; at run time the shader shifts the base by the difference between the live palette and the reference palette. Water flows, lights blink, and night maps come out right.
- **Colony**: the space colony is an 8-frame animation with its own sheet.
- **Result**: 131 bases (map 20 is 1792×2048) each with an index map, whole-image drawing.

## Unit art

![Unit art: original and HD](/media/hd-units.webp)

*Left: original, 96×96. Right: HD.*

![More unit art: original and HD](/media/hd-units-grid.webp)

*Eight more units, original left, HD right in each pair.*

- **Original**: 332 unit poses, mostly 96×96 or 128×128.
- **Why not AI redraws**: we tried Qwen Pro; some units were blocked by its copyright filter, and redrawing inevitably changes a unit's details. Instead we compared 14 local upscaling models and settled on **4x-UltraSharpV2 and 4x-PixelPerfectV4 blended half and half**. Two passes take it to 16×, then it is scaled down to an 8× master. Alpha goes through the model separately and its edges are cleaned.
- **Result**: shipped at 6× (at most 1024 pixels on the long side). The pre-battle and unit pages use it directly, and battle scenes replace the original sprite with it frame by frame. The original stores 134 poses mirrored; the vertex data tells which way to flip them, so units face the same way as in the original.

## Map unit icons

![Map unit icons: original and HD](/media/hd-icons.webp)

*Left: original, 16×16. Right: HD, 64×64.*

![Four faction colours](/media/hd-icons-factions.webp)

*One HD icon in the four faction colours.*

The icons on the map are only 16×16. The goal was **clearer, but still pixel art**.

- Pipeline: MMPX 2× → 4x-PixelPerfectV4 → hard-quantised back to 32×32 using only the original's colour indices (nearest in CIELAB) → MMPX 2× again, giving a 64×64 index map.
- Rejected along the way: Qwen (it invented shapes), smooth redraws ("no pixel feel"), dithering, xBRZ (GPL), Scale4x and others.
- The faction colour comes from the palette, so each icon is made once per faction: 1,256 images after removing duplicates, texture replacement.

## Battle scenes

Battle scenes are the most finely divided part of the pack: the picture is a stack of layers, each drawn its own way, and each needed its own approach.

![A battle: original and HD](/media/hd-battle-scene.webp)

*0.3.5 in game (God Gundam against Master Gundam, at a harbour). Original left, HD right in each pair. Top: the unit is HD art, and the sea and buildings have HD textures. Bottom: the attack effect is still the original.*

### The layers of a battle

| Layer | How the original draws it | What HD does |
| --- | --- | --- |
| Sky, starfield | 32×32 tiles, wrapping every 320×240 | Still original |
| Ground | 3 blocks × 3 rows of 3D models, textured by the renderer | 1,419 textures replaced at 4× |
| Units | Parts of an atlas stacked into a frame by a part table | Whole frames replaced with HD unit art |
| Extra unit images (afterimages, transformations, recolours) | The same | Derived or redrawn, 196 frames |
| Cut-in close-ups | The same | Upscaled locally, small faces redrawn; 308 frames (208 unique) |
| Weapons and props | The same | Upscaled locally; 431 frames (394 unique) |
| Attack effects, explosions, speed lines | Sprites with palette animation | Still original |
| HP and dialogue window frames | Texture slices | Redrawn by code (see "Frames" below) |

Units, extra images, cut-ins and props are the same thing in the original: a frame is a stack of small pieces from an atlas, laid out by a part table, each piece a quad that moves and scales with its node's matrix. HD does not swap the pieces one by one. It recognises which scene, atlas, palette and frame is being drawn, blanks the pieces, and draws one whole HD image in the same place under the same transform.

### Units

![Units in battle: original and HD](/media/hd-battle-units.webp)

*Units in battle up close, original left, HD right.*

- The units use the same 332 pieces of HD unit art as the pre-battle page.
- The original stores 134 poses mirrored, with every part flagged as flipped. The parts' vertex addresses tell which set is the mirrored one, so the HD art faces the same way as the original.
- The white flash when a unit is hit is done in two ways: one rewrites the palette in place, and the HD image does not change colour; the other switches to another palette resource, those frames are not recognised, and they fall back to the original.

### Extra unit images

Besides their standing poses, unit atlases hold afterimages, transformation frames, recolours, funnels flying out and more. Aligned against the poses one by one, they fall into six kinds, each handled differently:

| Kind | Count | How |
| --- | --- | --- |
| Reuses the pose as it is | 6 | The HD art |
| Recoloured or glowing | 11 | Recoloured from the HD art with a colour lookup |
| Afterimages and break-up effects | 8 | Derived from the HD art, row offsets estimated line by line |
| Pose with funnels flying out | 2 | The HD art plus the funnels |
| Partly changed (the pose with something added) | 47 | OpenAI's image model paints the changed part; the rest is the HD art |
| A completely new picture | 15 | Redrawn with OpenAI's image model |

![Derived extra images: original and HD](/media/hd-extras-derived.webp)

*Derived from the HD art: afterimages (Gundam Spiegel), a recolour (Master Gundam S), funnels flying out (ν Gundam). Original left, HD right in each pair.*

- **Deriving**: pixel by pixel, wherever the frame matches the pose it takes the HD art; where only the colour changed, it recolours by the colour ratio or a lookup; new pixels the pose does not have are upscaled from the original; empty pixels stay transparent. A single-hue glow maps pose brightness to the target colour.
- **Recolours, again**: some ratio recolours came out blurred and lost detail, so 8 of them now use a colour lookup: where the original frame and the pose overlap, each pose colour takes the target colour it most often becomes.

![A redrawn extra image](/media/hd-extras-imagegen.webp)

*Left: the original frame. Middle: the style reference given to the model (the HD art in the same colours). Right: the result.*

- **Redrawing**: 62 pictures went to OpenAI's image model, each with the original, the HD art in the same colours and its own prompt. The results were registered and matted to the original frame's outline as 8× masters. For partly changed pictures, a mask takes the grey areas from the HD art and the red areas from the generated picture, feathered by one original pixel.
- **Multi-frame animations**: only one frame is drawn; the others are derived from it.

### Cut-ins

![Locally upscaled cut-ins: original and HD](/media/hd-cutins.webp)

*Where local upscaling works well: a unit and a large face. Original left, HD right.*

- **Scope**: 55 close-up scenes, 308 frames (208 unique). Each frame is rendered from its part table, cut to the parts' box, and upscaled with the same local recipe as the unit art.
- **The problem**: where a face is only 6–20 original pixels, the upscaler invents features.

![Why some were redrawn](/media/hd-cutin-why.webp)

*Left: original. Middle: upscaled locally. Right: redrawn. In the bottom row's five figures, local upscaling smears the faces.*

- **The fix**: every cut-in frame is cut from a whole drawing stored in the atlas (a pan, or figures gathering, is the same drawing moved). So instead of redrawing frame by frame, those 10 whole drawings went to OpenAI's image model; the redraws were registered, matted, pasted back into an 8× atlas, and 36 frames were rebuilt from the original part tables (stacking order, horizontal flips).

![The whole drawing in the atlas and its redraw](/media/hd-cutin-atlas.webp)

*The whole drawing stored in the atlas (left) and the redraw (right).*

- **Details**: one frame lays 5 eye patches from elsewhere (15×7 pixels each) over the drawing; those are left out of the rebuild and the eyes painted separately. Large-face close-ups already match the original well when upscaled and were not redrawn.

### Weapons and props

![Props: original and HD](/media/hd-props.webp)

*Weapons and props, original left, HD right in each pair.*

Everything in the unit atlases that is not a pose, an extra image or a cut-in counts as a prop: 289 scenes, 431 frames (394 unique) of rifles, beam sabres, shields, fists and so on, upscaled with the same recipe and shipped with the cut-ins.

### Ground

![Battle ground textures: original and HD](/media/hd-ground.webp)

*Battle ground textures. Left: original, 64×64. Right: HD, 256×256.*

- **Scale**: 424 3D ground models with 1,419 textures.
- **Hashes without running the game**: texture replacement needs the hash the renderer gives each texture. We simulate how the display lists load textures into the 4 KB texture memory (odd rows word-swapped, palettes copied four times) and hash the result the way RT64 does. All 16 hashes captured from a running game in the pilot came out the same.
- **Upscaling**: each texture is first extended by 16 pixels according to how it wraps, then upscaled 4×, so tiles join without seams. Noisy textures such as grass and gravel come out a little painterly; we accepted that.

### Size, and what is still original

- **Pack size**: whole frames that only appear in battle are packed at 5× the original. Units are scaled 80–110% in battle and cut-ins about 129%, so at the default 4× internal resolution one original pixel covers about 3.2–5.2 screen pixels; 5× is enough. The 8× masters stay local.
- **Still original**: skies and starfields; attack effects, explosions and speed lines, which change colour through palette animation and would freeze as a fixed HD image; and the hit-flash frames mentioned above.
## Frames, the title and story screens

![Title logo: original and HD](/media/hd-title.webp)

*Left: original. Right: HD.*

![Title flames: original and HD](/media/hd-flames.webp)

*The title screen's flames (2 of the 16 pieces), original left, HD right in each pair.*

- **Dialogue and HUD frames**: 13 dialogue frame slices and 17 battle HUD slices, redrawn by code after the original design (16 pixels to 64), texture replacement.
- **Window frames, BANPRESTO, GAME OVER**: generated at run time from your ROM, not in the HD pack, no AI. The frames are anti-aliased round-capped strokes that keep the original colour indices, so the original's light running along the frame still plays.
- **Title logo and flames**: made with Qwen, colours locked, and the flames cut into 16 pieces that loop seamlessly.
- **Text painted into images**: the title menu, 133 chapter cards, the prologue, the ending and the credits are all typeset again in the current language; none of the original's Japanese images remain.

## Size

The 0.3.5 HD pack is 505 MB zipped, about 540 MB unpacked, 6,795 files. The largest parts are the tactical maps (246 MB) and the PNGs for texture replacement (161 MB).

- Whole images are stored as JPEG (quality 92), with a separate alpha PNG where transparency is needed. Index maps, animation frames and replacement textures stay PNG (RT64 accepts only PNG and DDS).
- Unit art is reduced from 8× to 6×, since the pages show it at about 1,000 pixels at most.
- BC7 texture compression made the world map close-ups blocky and was not used. Sampled quality (PSNR): unit art 44 dB, portraits 42 dB, maps 41 dB.

## Where AI was used

The pictures in the HD pack come from different places, so here they are in one list:

- **Alibaba Cloud Qwen image models** (qwen-image-3.0 / 3.0-pro): character portraits, intermission backgrounds, space objects, the title logo and flames.
- **Alibaba Cloud Wanx** (wanx2.1-imageedit): widening the intermission backgrounds to 16:9.
- **OpenAI's image model** (through Codex): the story world map, the tactical map bases, and some battle cut-ins and extra unit images.
- **Local upscaling models** (4x-UltraSharpV2, 4x-PixelPerfectV4, plus MMPX pixel scaling): unit art, map unit icons, battle cut-ins and props, battle ground textures, and the tactical maps' index maps. These are enlarged directly from the original pixels.
- **No AI**: ship, landmark and marker models (built by hand), dialogue and HUD frames (drawn by code), window frames and logos (generated from your ROM at run time), and all text (native typesetting).

Every AI-generated or upscaled picture used the original graphics as its reference and was aligned back to the original composition one by one. This is an unofficial fan project; the original characters and art belong to their owners.
