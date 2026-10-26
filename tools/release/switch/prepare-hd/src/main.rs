//! The HD pack prepared for the Switch's GPU, for the NRO's romfs (src/host/hd_texture.hpp).
//!
//! Reads a released HD pack folder (tools/release/prepare_hd_bundle.py; build_release.py puts
//! one at build/release/<version>/pack/hd) and writes build/recomp/switch-romfs/hd/: the pack's
//! manifests as they are, and beside each picture of the layers the Switch draws in HD its
//! <stem>.mwtex: premultiplied RGBA with a 2 x 2 box-filtered mip chain (as the layers' own
//! decode() makes it), each level BC7-compressed (Intel's ISPC encoder), all levels in one zstd
//! frame: a quarter of RGBA8's memory, uploaded as it is. Not ASTC: the Switch's Mesa (nouveau,
//! 20.1) lists ASTC but decodes it on the CPU at upload, some 70 ms for a 512 x 576 picture with
//! its mips; BC7 the GPU reads itself (docs/design/switch-port.md, S3). A tactical map's index.png goes raw (GL_R8): its palette
//! indices pick each texel's palette shift on the GPU. The JPEGs and PNGs stay out of the romfs,
//! except the portraits (the pages show them too) and the sprite layer's (units, unit extras,
//! battle sprites, scene images): a frame the game recolours (a hit flash, a gold mode) is made
//! from the picture (native_sprite.cpp), and the battle scenes' thumbnails are shown as they are. The RT64 texture pack (rt64.json: map
//! unit icons, the dialogue frame, the world map, space, battle grounds and props) keeps straight
//! alpha, as RT64 samples it (src/host/hd_replace.cpp): its pixel-art icons and frame as RGBA8,
//! the rest BC7, each with mips averaged by alpha.
//!
//!   cargo run --release --manifest-path tools/release/switch/prepare-hd/Cargo.toml -- \
//!       [--pack DIR] [--layers portraits,backgrounds,maps,sprites,rt64] [--jobs N]
//!
//! Pictures already prepared and newer than their source are left as they are; after a change
//! of format, remove build/recomp/switch-romfs/hd first.

use rayon::prelude::*;
use serde_json::Value;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::exit;
use std::sync::atomic::{AtomicUsize, Ordering};

const MAGIC: u32 = 0x5854_574D;
const VERSION: u32 = 1;
const BC7: u32 = 0x8E8C;   // GL_COMPRESSED_RGBA_BPTC_UNORM
const R8: u32 = 0x8229;
const RGBA8: u32 = 0x8058;

type Result<T> = std::result::Result<T, String>;

#[derive(Clone)]
enum Kind {
    Bc7 { straight: bool },
    Rgba,
    Index,
}

struct Job {
    picture: PathBuf,
    target: PathBuf,
    kind: Kind,
}

/// The picture with its alpha, as presentation::load_rgba reads it (a JPEG's alpha beside it,
/// <stem>.alpha.png, in its alpha channel or as grey).
fn rgba(picture: &Path) -> Result<image::RgbaImage> {
    let mut image = image::open(picture).map_err(|e| format!("{}: {e}", picture.display()))?.to_rgba8();
    let alpha = picture.with_extension("alpha.png");
    if alpha.exists() {
        let mask = image::open(&alpha).map_err(|e| format!("{}: {e}", alpha.display()))?;
        let values: Vec<u8> = if mask.color().has_alpha() {
            mask.to_rgba8().pixels().map(|p| p[3]).collect()
        } else {
            mask.to_luma8().into_raw()
        };
        if (mask.width(), mask.height()) != image.dimensions() {
            return Err(format!("{}: {}x{} for a {}x{} picture", alpha.display(), mask.width(), mask.height(), image.width(), image.height()));
        }
        for (pixel, a) in image.pixels_mut().zip(values) {
            pixel[3] = a;
        }
    }
    Ok(image)
}

/// Level 0 premultiplied (rounded as decode() does), then 2 x 2 averages down to 1 x 1, an odd
/// edge clamped (native_background.cpp's decode, gpu::rgba_mips).
fn mips(image: image::RgbaImage) -> Vec<(u32, u32, Vec<u8>)> {
    let (mut w, mut h) = image.dimensions();
    let mut level = image.into_raw();
    for px in level.chunks_exact_mut(4) {
        let a = px[3] as u32;
        for c in 0..3 {
            px[c] = ((px[c] as u32 * a + 127) / 255) as u8;
        }
    }
    let mut levels = vec![(w, h, level)];
    while w > 1 || h > 1 {
        let (sw, sh) = (w as usize, h as usize);
        let (dw, dh) = ((w / 2).max(1), (h / 2).max(1));
        let src = &levels.last().unwrap().2;
        let mut next = vec![0u8; dw as usize * dh as usize * 4];
        for y in 0..dh as usize {
            let (y0, y1) = ((2 * y).min(sh - 1), (2 * y + 1).min(sh - 1));
            for x in 0..dw as usize {
                let (x0, x1) = ((2 * x).min(sw - 1), (2 * x + 1).min(sw - 1));
                for c in 0..4 {
                    let at = |xx: usize, yy: usize| src[(yy * sw + xx) * 4 + c] as u32;
                    next[(y * dw as usize + x) * 4 + c] = ((at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1) + 2) / 4) as u8;
                }
            }
        }
        levels.push((dw, dh, next));
        w = dw;
        h = dh;
    }
    levels
}

fn write(target: &Path, format: u32, width: u32, height: u32, levels: u32, data: &[u8]) -> Result<()> {
    let packed = zstd::encode_all(data, 19).map_err(|e| e.to_string())?;
    let mut file = Vec::with_capacity(24 + packed.len());
    for word in [MAGIC, VERSION, format, width, height, levels] {
        file.extend_from_slice(&word.to_le_bytes());
    }
    file.extend_from_slice(&packed);
    fs::create_dir_all(target.parent().unwrap()).map_err(|e| e.to_string())?;
    fs::write(target, file).map_err(|e| format!("{}: {e}", target.display()))
}

/// The mips with straight alpha again: level 0 as it was, the smaller ones averaged by alpha.
fn straight_mips(image: image::RgbaImage) -> Vec<(u32, u32, Vec<u8>)> {
    let original = image.as_raw().clone();
    let mut levels = mips(image);
    levels[0].2 = original;
    for (_, _, level) in levels.iter_mut().skip(1) {
        for px in level.chunks_exact_mut(4) {
            let a = px[3] as u32;
            for c in 0..3 {
                px[c] = if a == 0 { 0 } else { ((px[c] as u32 * 255 + a / 2) / a).min(255) as u8 };
            }
        }
    }
    levels
}

/// Uncompressed straight RGBA8 with its mips (pixel art that blocks would smear).
fn raw(job: &Job) -> Result<usize> {
    let image = rgba(&job.picture)?;
    let (w, h) = image.dimensions();
    let levels = straight_mips(image);
    let data: Vec<u8> = levels.iter().flat_map(|(_, _, l)| l.iter().copied()).collect();
    write(&job.target, RGBA8, w, h, levels.len() as u32, &data)?;
    Ok(data.len())
}

/// Each level as BC7 blocks (Intel's ISPC encoder), the level padded to whole blocks by
/// repeating its edge.
fn encode(job: &Job, straight: bool) -> Result<usize> {
    let image = rgba(&job.picture)?;
    let (w, h) = image.dimensions();
    let opaque = image.pixels().all(|p| p[3] == 255);
    let levels = if straight { straight_mips(image) } else { mips(image) };
    let settings = if opaque { intel_tex_2::bc7::opaque_basic_settings() } else { intel_tex_2::bc7::alpha_basic_settings() };
    let mut blocks = Vec::new();
    for (lw, lh, level) in &levels {
        let (pw, ph) = ((lw + 3) / 4 * 4, (lh + 3) / 4 * 4);
        let mut padded = vec![0u8; (pw * ph * 4) as usize];
        for y in 0..ph {
            let sy = y.min(lh - 1);
            for x in 0..pw {
                let sx = x.min(lw - 1);
                let from = ((sy * lw + sx) * 4) as usize;
                let to = ((y * pw + x) * 4) as usize;
                padded[to..to + 4].copy_from_slice(&level[from..from + 4]);
            }
        }
        let surface = intel_tex_2::RgbaSurface { data: &padded, width: pw, height: ph, stride: pw * 4 };
        blocks.extend_from_slice(&intel_tex_2::bc7::compress_blocks(&settings, &surface));
    }
    write(&job.target, BC7, w, h, levels.len() as u32, &blocks)?;
    Ok(blocks.len())
}

fn index(job: &Job) -> Result<usize> {
    let image = image::open(&job.picture).map_err(|e| format!("{}: {e}", job.picture.display()))?.to_luma8();
    let (w, h) = image.dimensions();
    let raw = image.into_raw();
    write(&job.target, R8, w, h, 1, &raw)?;
    Ok(raw.len())
}

fn json(path: &Path) -> Value {
    let text = fs::read_to_string(path).unwrap_or_else(|e| fail(&format!("{}: {e}", path.display())));
    serde_json::from_str(&text).unwrap_or_else(|e| fail(&format!("{}: {e}", path.display())))
}

fn fail(message: &str) -> ! {
    eprintln!("prepare-hd: {message}");
    exit(1)
}

fn root() -> PathBuf {
    // tools/release/switch/prepare-hd: the repository four levels up.
    Path::new(env!("CARGO_MANIFEST_DIR")).ancestors().nth(4).unwrap().to_path_buf()
}

fn latest_pack(root: &Path) -> PathBuf {
    let mut packs: Vec<PathBuf> = fs::read_dir(root.join("build/release"))
        .into_iter()
        .flatten()
        .flatten()
        .map(|e| e.path().join("pack/hd"))
        .filter(|p| p.is_dir())
        .collect();
    packs.sort_by_key(|p| fs::metadata(p).and_then(|m| m.modified()).ok());
    packs.pop().unwrap_or_else(|| fail("no HD pack under build/release/*/pack/hd: pass --pack"))
}

fn meta_files(folder: &Path, out: &mut Vec<PathBuf>) {
    let Ok(entries) = fs::read_dir(folder) else { return };
    let mut entries: Vec<_> = entries.flatten().map(|e| e.path()).collect();
    entries.sort();
    for path in entries {
        if path.is_dir() {
            meta_files(&path, out);
        } else if path.file_name().is_some_and(|n| n == "meta.json") {
            out.push(path);
        }
    }
}

/// The picture as it is (and its alpha beside it), for what reads it as a picture.
fn copy_picture(picture: &Path, target: &Path) {
    for (from, to) in [(picture.to_path_buf(), target.to_path_buf()), (picture.with_extension("alpha.png"), target.with_extension("alpha.png"))] {
        if from.exists() && stale(&from, &to) {
            fs::create_dir_all(to.parent().unwrap()).unwrap_or_else(|e| fail(&e.to_string()));
            fs::copy(&from, &to).unwrap_or_else(|e| fail(&format!("{}: {e}", from.display())));
        }
    }
}

fn stale(picture: &Path, target: &Path) -> bool {
    let time = |p: &Path| fs::metadata(p).and_then(|m| m.modified()).ok();
    match (time(picture), time(target)) {
        (Some(source), Some(made)) => made < source,
        _ => true,
    }
}

fn main() {
    let root = root();
    let mut pack = None;
    let mut layers = "portraits,backgrounds,maps,sprites,rt64".to_string();
    let mut jobs_wanted = 0usize;
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--pack" => pack = args.next().map(PathBuf::from),
            "--layers" => layers = args.next().unwrap_or_default(),
            "--jobs" => jobs_wanted = args.next().and_then(|n| n.parse().ok()).unwrap_or(0),
            _ => fail(&format!("unknown argument {arg} (--pack DIR, --layers a,b, --jobs N)")),
        }
    }
    let pack = pack.unwrap_or_else(|| latest_pack(&root));
    let art = pack.join("art");
    let out = root.join("build/recomp/switch-romfs/hd");
    let out_art = out.join("art");
    fs::create_dir_all(&out_art).unwrap_or_else(|e| fail(&e.to_string()));
    for name in ["hd.json", "NOTICE.txt"] {
        if pack.join(name).exists() {
            fs::copy(pack.join(name), out.join(name)).unwrap_or_else(|e| fail(&e.to_string()));
        }
    }
    // The model packs as they are (native_marker.cpp reads them).
    for folder in ["native-marker", "native-models"] {
        let mut stack = vec![pack.join(folder)];
        while let Some(dir) = stack.pop() {
            for entry in fs::read_dir(&dir).into_iter().flatten().flatten() {
                let path = entry.path();
                if path.is_dir() {
                    stack.push(path);
                } else {
                    copy_picture(&path, &out.join(path.strip_prefix(&pack).unwrap()));
                }
            }
        }
    }
    for entry in fs::read_dir(&art).unwrap_or_else(|e| fail(&format!("{}: {e}", art.display()))).flatten() {
        let path = entry.path();
        if path.extension().is_some_and(|x| x == "json") {
            fs::copy(&path, out_art.join(path.file_name().unwrap())).unwrap_or_else(|e| fail(&e.to_string()));
        }
    }

    let mut jobs = Vec::new();
    // The baked battle scenery's atlas (16 MB of PNG for 5598) as BC7 too.
    let models = pack.join("native-models");
    if models.join("manifest.json").exists() {
        for row in json(&models.join("manifest.json"))["models"].as_array().unwrap_or(&Vec::new()) {
            if let Some(texture) = row["texture"].as_str() {
                let picture = models.join(texture);
                let target = out.join("native-models").join(texture).with_extension("mwtex");
                if stale(&picture, &target) {
                    jobs.push(Job { picture, target, kind: Kind::Bc7 { straight: true } });
                }
            }
        }
    }
    for layer in layers.split(',').filter(|l| !l.is_empty()) {
        match layer {
            "portraits" | "backgrounds" => {
                let (spec, kind) = if layer == "portraits" {
                    ("srw64-portraits-hd.json", Kind::Bc7 { straight: false })
                } else {
                    ("srw64-backgrounds-hd.json", Kind::Bc7 { straight: false })
                };
                for row in json(&art.join(spec))["images"].as_array().unwrap_or(&Vec::new()) {
                    let file = row["file"].as_str().unwrap_or_else(|| fail("an image without a file"));
                    let picture = art.join(file);
                    // The pages show the portraits too, decoded as they are (launch.cpp attach_hd_art).
                    if layer == "portraits" {
                        copy_picture(&picture, &out_art.join(file));
                    }
                    let target = out_art.join(file).with_extension("mwtex");
                    if stale(&picture, &target) {
                        jobs.push(Job { picture, target, kind: kind.clone() });
                    }
                }
            }
            "maps" => {
                let index_json = json(&art.join("srw64-tactical-maps.json"));
                let maps = art.join(index_json["root"].as_str().unwrap_or("maps"));
                let mut metas = Vec::new();
                meta_files(&maps, &mut metas);
                // Every map's meta.json in one file as well (native_map.cpp reads it at start
                // instead of 131 folders' worth of lookups on the SD card).
                let mut all = serde_json::Map::new();
                for meta_path in &metas {
                    let folder = meta_path.parent().unwrap().strip_prefix(&maps).unwrap();
                    all.insert(folder.to_string_lossy().replace('\\', "/"), json(meta_path));
                }
                let out_maps = out_art.join(maps.strip_prefix(&art).unwrap());
                fs::create_dir_all(&out_maps).unwrap_or_else(|e| fail(&e.to_string()));
                let index = serde_json::json!({"schema": "srw64.hd-map-metas.v1", "maps": all});
                fs::write(out_maps.join("metas.json"), index.to_string()).unwrap_or_else(|e| fail(&e.to_string()));
                for meta_path in metas {
                    let folder = meta_path.parent().unwrap();
                    let out_folder = out_art.join(folder.strip_prefix(&art).unwrap());
                    fs::create_dir_all(&out_folder).unwrap_or_else(|e| fail(&e.to_string()));
                    fs::copy(&meta_path, out_folder.join("meta.json")).unwrap_or_else(|e| fail(&e.to_string()));
                    let meta = json(&meta_path);
                    let base = folder.join(meta["base"].as_str().unwrap_or("base.png"));
                    let base_target = out_folder.join(base.file_name().unwrap()).with_extension("mwtex");
                    if stale(&base, &base_target) {
                        jobs.push(Job { picture: base, target: base_target, kind: Kind::Bc7 { straight: false } });
                    }
                    let index_picture = folder.join("index.png");
                    let index_target = out_folder.join("index.mwtex");
                    if stale(&index_picture, &index_target) {
                        jobs.push(Job { picture: index_picture, target: index_target, kind: Kind::Index });
                    }
                }
            }
            "sprites" => {
                for spec in ["srw64-units-hd.json", "srw64-unit-extras-hd.json", "srw64-battle-sprites-hd.json", "srw64-scene-images.json"] {
                    let spec = json(&art.join(spec));
                    for row in spec["images"].as_array().unwrap_or(&Vec::new()) {
                        let file = row["file"].as_str().unwrap_or_else(|| fail("an image without a file"));
                        let picture = art.join(file);
                        copy_picture(&picture, &out_art.join(file));
                        let target = out_art.join(file).with_extension("mwtex");
                        if stale(&picture, &target) {
                            jobs.push(Job { picture, target, kind: Kind::Bc7 { straight: false } });
                        }
                    }
                    for row in spec["scenes"].as_array().unwrap_or(&Vec::new()) {
                        let file = row["file"].as_str().unwrap_or_else(|| fail("a scene without a file"));
                        copy_picture(&art.join(file), &out_art.join(file));
                    }
                }
            }
            "rt64" => {
                for row in json(&art.join("rt64.json"))["textures"].as_array().unwrap_or(&Vec::new()) {
                    let file = row["path"].as_str().unwrap_or_else(|| fail("a texture without a path"));
                    let picture = art.join(file);
                    let target = out_art.join(file).with_extension("mwtex");
                    let kind = match row["kind"].as_str() {
                        Some("icon") | Some("frame") => Kind::Rgba,
                        _ => Kind::Bc7 { straight: true },
                    };
                    if stale(&picture, &target) {
                        jobs.push(Job { picture, target, kind });
                    }
                }
            }
            _ => fail(&format!("unknown layer {layer} (portraits, backgrounds, maps, sprites, rt64)")),
        }
    }

    println!("{} picture(s) to prepare from {}", jobs.len(), pack.display());
    if jobs_wanted > 0 {
        rayon::ThreadPoolBuilder::new().num_threads(jobs_wanted).build_global().ok();
    }
    let done = AtomicUsize::new(0);
    let total = jobs.len();
    let bytes: Result<Vec<usize>> = jobs
        .par_iter()
        .map(|job| {
            let made = match &job.kind {
                Kind::Bc7 { straight } => encode(job, *straight),
                Kind::Rgba => raw(job),
                Kind::Index => index(job),
            };
            let n = done.fetch_add(1, Ordering::Relaxed) + 1;
            if n % 50 == 0 || n == total {
                println!("  {n}/{total}");
            }
            made
        })
        .collect();
    let bytes = bytes.unwrap_or_else(|e| fail(&e));
    let mut size = 0u64;
    let mut stack = vec![out.clone()];
    while let Some(dir) = stack.pop() {
        for entry in fs::read_dir(&dir).into_iter().flatten().flatten() {
            let path = entry.path();
            if path.is_dir() {
                stack.push(path);
            } else {
                size += entry.metadata().map(|m| m.len()).unwrap_or(0);
            }
        }
    }
    println!(
        "{}: {:.1} MiB ({:.1} MiB of blocks before zstd this run)",
        out.display(),
        size as f64 / 1048576.0,
        bytes.iter().sum::<usize>() as f64 / 1048576.0
    );
}
