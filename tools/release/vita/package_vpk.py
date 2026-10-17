#!/usr/bin/env python3
"""The PS Vita package (docs/design/psv-port.md, V2): eboot.bin and param.sfo from
build_game.sh's build, the icon from the website's M64 badge, and beside the program what
the Linux package carries beside its own (build_linux.py): the dialogue text, the fonts and
the stages' metadata. A VPK is a zip; the Vita installs it as is.

  package_vpk.py [--build build/recomp/vita-build] [--output build/recomp/vita-build/Marchwind64.vpk]
"""
import argparse
import io
from pathlib import Path
import zipfile

from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--build", type=Path, default=ROOT / "build/recomp/vita-build")
parser.add_argument("--output", type=Path)
args = parser.parse_args()
output = args.output or args.build / "Marchwind64.vpk"

# The LiveArea icon: 128 x 128, 8-bit palette (the system rejects others).
icon = Image.open(ROOT / "web/public/brand/icon-180.png").convert("RGBA").resize((128, 128), Image.LANCZOS)
plate = Image.new("RGBA", icon.size, (11, 19, 36, 255))
plate.alpha_composite(icon)
png = io.BytesIO()
plate.convert("RGB").quantize(256).save(png, format="PNG")

files = [(args.build / "eboot.bin", "eboot.bin"), (args.build / "param.sfo", "sce_sys/param.sfo")]
for folder, name in ((ROOT / "content/dialogue", "dialogue"), (ROOT / "build/fonts", "fonts"), (ROOT / "content/stages", "stages")):
    for path in sorted(folder.resolve().rglob("*")):
        if path.is_file() and not path.name.startswith(".") and (name != "dialogue" or path.suffix == ".txt"):
            files.append((path, f"{name}/{path.relative_to(folder.resolve()).as_posix()}"))
output.parent.mkdir(parents=True, exist_ok=True)
temporary = output.with_suffix(".tmp")
with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED) as vpk:
    vpk.writestr("sce_sys/icon0.png", png.getvalue())
    for path, name in files:
        vpk.write(path, name)
temporary.replace(output)
print(f"SRW64_VITA_VPK {output} {output.stat().st_size} bytes, {len(files) + 1} files")
