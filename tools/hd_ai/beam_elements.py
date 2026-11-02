"""Beam effect elements drawn in the manner of Super Robot Wars Z's battle effects.

Z draws its beams from small 4-bit intensity sprites coloured through a palette ramp (see
docs/design/battle-animation-rendering.md). These are our own drawings of the same kinds of
sprite, made procedurally at any size, so none of Z's art is shipped: a glow ball, a soft beam
bar, a thin straight line, a puff of flow lines, a muzzle burst, a star flash, a long tapering
streak and a scatter of sparks. Each returns a float32 intensity map, 0..1, height x width; the
beam is to fly toward -x (head on the left). The profiles follow measurements of Z's sprites.

python -m tools.hd_ai.beam_elements OUT.png   draws them all as a sheet
"""
from __future__ import annotations

import sys

import numpy as np
from scipy import ndimage

# radial intensity of Z's glow ball, centre to rim, at even steps
GLOW_PROFILE = (1, 1, 1, 1, .96, .91, .85, .75, .62, .5, .4, .33, .25, .19, .12, .06, 0)


def _grid(h: int, w: int) -> tuple[np.ndarray, np.ndarray]:
    """u along (0..1, left to right), v across (-1..1, top to bottom)."""
    v, u = np.mgrid[0:h, 0:w].astype(np.float32)
    return u / max(w - 1, 1), v / max(h - 1, 1) * 2 - 1


def glow(size: int) -> np.ndarray:
    """A round glow: a flat white middle, then a long soft fall to the rim."""
    u, v = _grid(size, size)
    r = np.hypot(u * 2 - 1, v)
    return np.interp(r, np.linspace(0, 1, len(GLOW_PROFILE)), GLOW_PROFILE).astype(np.float32)


def bar(h: int, w: int, seed: int = 0) -> np.ndarray:
    """A soft beam bar: a white body coming to a jagged point at the head, widest a third of the
    way along, tapering to a thread at the tail; top and bottom edges ragged apart."""
    rng = np.random.default_rng(seed)
    u, v = _grid(h, w)
    # half-width along the bar, from Z's: pointed head, broad shoulder, long taper
    half = np.interp(u, (0, .03, .1, .2, .45, .6, .75, .9, 1), (.04, .4, .62, .7, .62, .42, .2, .08, .03))
    def ragged(scale):
        n = ndimage.gaussian_filter1d(rng.normal(0, 1, w), max(w / 30, 1))
        return n / max(np.abs(n).max(), 1e-6) * scale
    top = half * (1 + ragged(.15)); bottom = half * (1 + ragged(.15))
    edge = np.where(v < 0, top, bottom)
    d = np.abs(v) / np.maximum(edge, 1e-3)
    body = np.clip(2.2 - 2.2 * d ** 2, 0, 1)                     # a wide white plateau, a soft edge
    halo = np.exp(-(np.maximum(d - .85, 0) * 2.5) ** 2) * .3
    along = np.interp(u, (0, .05, .85, 1), (.5, 1, .95, .55))
    return np.clip(np.maximum(body, halo) * along, 0, 1).astype(np.float32)


def line(h: int, w: int) -> np.ndarray:
    """A thin straight line: a white core with a short grey falloff, ends rounded off."""
    u, v = _grid(h, w)
    across = np.interp(np.abs(v), (0, .15, .45, .75, 1), (1, .7, .3, .15, 0))
    ends = np.clip(np.minimum(u, 1 - u) / .05, 0, 1) ** .7
    return (across * ends).astype(np.float32)


def flow(h: int, w: int, seed: int = 0, streaks: int | None = None) -> np.ndarray:
    """A puff of flow lines: many hairline streaks along x in two loose bunches, each starting
    bright near the left and fading out along its length to the right."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w), np.float32)
    x = np.arange(w, dtype=np.float32)
    streaks = streaks or 150
    bunches = (rng.uniform(.15, .4), rng.uniform(.65, .9))
    for _ in range(streaks):
        row = int(np.clip(rng.normal(bunches[rng.integers(2)], .08), 0, .999) * h)
        length = rng.uniform(.4, 1.1) * w
        start = rng.uniform(-.1, .35) * w
        t = np.clip((x - start) / length, 0, 1)
        streak = np.where((x > start) & (t < 1), np.clip(t / .15, 0, 1) * (1 - t) ** 1.4, 0)
        out[row] = np.maximum(out[row], rng.uniform(.25, 1) * streak)
    out = ndimage.gaussian_filter(out, (max(h / 160, .6), w / 200))
    u, _ = _grid(h, w)
    out *= np.interp(u, (0, .08, .4, 1), (.15, 1, .9, .1))
    return (out / max(out.max(), 1e-6)).astype(np.float32)


def burst(h: int, w: int, seed: int = 0, spikes: int = 14) -> np.ndarray:
    """A muzzle burst: soft spikes fanning out to the right from a point on the left edge, the
    longest along the middle, with a bright knot at the root."""
    rng = np.random.default_rng(seed)
    u, v = _grid(h, w)
    x, y = u * w / h, v / 2                                     # same scale both ways, root at (0, 0)
    r = np.hypot(x, y); ang = np.arctan2(y, np.maximum(x, 1e-6))
    out = np.zeros((h, w), np.float32)
    for _ in range(spikes + 8):
        a = rng.normal(0, .5)
        length = (1 - abs(a) / 1.4) * rng.uniform(.45, 1) * w / h
        width = rng.uniform(.07, .2)
        spike = np.exp(-((ang - a) / width) ** 2) * np.clip(1 - r / max(length, 1e-3), 0, 1) ** .6
        out = np.maximum(out, spike * rng.uniform(.7, 1.2))
    lumpy = ndimage.gaussian_filter(rng.uniform(0, 1, (h, w)), max(h / 14, 1))
    lumpy = (lumpy - lumpy.min()) / max(np.ptp(lumpy), 1e-6)
    out = out * (.55 + .9 * lumpy) + np.exp(-(r / .3) ** 2) * .5
    return np.clip((out - .12) * 1.8, 0, 1).astype(np.float32)          # hard flame edges


def star(size: int, points: int = 7, seed: int = 0) -> np.ndarray:
    """A star flash: a white disc with short rounded points and a soft rim."""
    rng = np.random.default_rng(seed)
    u, v = _grid(size, size)
    x, y = u * 2 - 1, v
    r = np.hypot(x, y); ang = np.arctan2(y, x) + rng.uniform(0, np.pi)
    jitter = np.interp(ang % (2 * np.pi), np.linspace(0, 2 * np.pi, points + 1), np.r_[q := rng.uniform(.8, 1.2, points), q[0]])
    tri = 1 - 2 * np.abs((ang * points / (2 * np.pi)) % 1 - .5)       # 1 at each point, 0 between
    edge = .6 + .3 * jitter * tri ** 2.2
    return np.clip((edge - r) / .2 + .5, 0, 1).astype(np.float32)


def taper(h: int, w: int) -> np.ndarray:
    """Long tapering streaks: a thin wedge above a wide one, each from a point on the left
    widening and brightening to the right, the wide end fading out short of the edge."""
    u, v = _grid(h, w)
    out = np.zeros(u.shape, np.float32)
    for centre, width, reach, peak in ((-.55, .12, .78, .75), (.35, .5, .92, 1)):
        t = np.clip(u / reach, 0, 1)
        half = .015 + width * t ** 1.1
        d = np.abs(v - centre - width * .3 * t) / np.maximum(half, 1e-3)
        across = np.clip((1 - d) * 3, 0, 1)
        fade = np.interp(u, (0, .05, reach * .85, reach, reach + .04), (.2, .7, 1, .6, 0))
        out = np.maximum(out, across * (.5 + .5 * t) * fade * peak)
    return out


def sparks(h: int, w: int, seed: int = 0, count: int | None = None, unit: float | None = None) -> np.ndarray:
    """A scatter of sparks: small soft dots, a few larger, each with a white centre."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w), np.float32)
    unit = unit or min(h, w) / 40                               # a small spark's radius
    count = count or max(6, int(h * w / unit ** 2 / 138))
    yy, xx = np.mgrid[0:h, 0:w]
    for _ in range(count):
        cy, cx = rng.uniform(0, h), rng.uniform(0, w)
        s = unit * rng.choice((1.1, 1.5, 2.3), p=(.5, .35, .15))
        d2 = ((yy - cy) ** 2 + (xx - cx) ** 2) / (2 * s * s)
        out = np.maximum(out, np.clip(np.exp(-d2) * .6 + np.exp(-d2 * 5) * .6, 0, 1) * rng.uniform(.6, 1))
    return out


ALL = {
    'glow': lambda k: glow(94 * k),
    'bar': lambda k: bar(25 * k, 93 * k),
    'line': lambda k: line(9 * k, 91 * k),
    'flow': lambda k: flow(77 * k, 80 * k),
    'burst': lambda k: burst(47 * k, 71 * k),
    'star': lambda k: star(33 * k),
    'taper': lambda k: taper(33 * k, 124 * k),
    'sparks': lambda k: sparks(69 * k, 40 * k),
}


def main() -> None:
    from PIL import Image
    tiles = [Image.fromarray((f(4) * 255).astype(np.uint8)) for f in ALL.values()]
    sheet = Image.new('L', (max(t.width for t in tiles) + 20, sum(t.height + 20 for t in tiles)), 30)
    y = 10
    for t in tiles:
        sheet.paste(t, (10, y)); y += t.height + 20
    sheet.save(sys.argv[1])


if __name__ == '__main__':
    main()
