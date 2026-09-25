#!/bin/sh
# Build the Linux x64 package from macOS (or any Docker host) in the pinned
# Ubuntu 22.04 container. Run `make` first; see docs/guide/linux-build.md.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
python=${PYTHON:-$root/.venv/bin/python}
# Apply the checked RT64 patches here, where git owns the checkout.
"$python" "$root/tools/recomp/toolchain/prepare_rt64.py" >/dev/null
docker build --platform linux/amd64 -t srw64-linux-build:jammy "$root/tools/release/linux"
# Same absolute path inside the container, so generated files name the same sources.
exec docker run --rm --platform linux/amd64 -v "$root:$root" -w "$root" \
    -e HOME=/tmp -e PYTHONUTF8=1 srw64-linux-build:jammy \
    python3.11 tools/release/build_linux.py "$@"
