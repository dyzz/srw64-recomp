"""Palette-preserving pixel-art magnification on index images (no new colours).

Both filters only copy source indices, so one result serves every palette:
faction colours for unit icons, live palette cycles for tactical maps.
MMPX follows McGuire & Gagiu, "MMPX Style-Preserving Pixel Art Magnification"
(JCGT 2021, MIT-licensed reference code); luma comes from a chosen palette.
"""
from __future__ import annotations

from PIL import Image


def _sampler(width: int, height: int, pixels: list[int]):
    def at(x: int, y: int) -> int:
        return pixels[min(max(y, 0), height - 1) * width + min(max(x, 0), width - 1)]
    return at


def scale2x(width: int, height: int, pixels: list[int]) -> tuple[int, int, list[int]]:
    """EPX / Scale2x."""
    at = _sampler(width, height, pixels)
    out = [0] * (4 * width * height)
    row = 2 * width
    for y in range(height):
        for x in range(width):
            p, a, b, c, d = at(x, y), at(x, y - 1), at(x + 1, y), at(x - 1, y), at(x, y + 1)
            top, bottom = 2 * y * row + 2 * x, (2 * y + 1) * row + 2 * x
            out[top] = c if c == a and c != d and a != b else p
            out[top + 1] = a if a == b and a != c and b != d else p
            out[bottom] = c if d == c and d != b and c != a else p
            out[bottom + 1] = b if b == d and b != a and d != c else p
    return 2 * width, 2 * height, out


def mmpx2x(width: int, height: int, pixels: list[int], luma: list[int]) -> tuple[int, int, list[int]]:
    at = _sampler(width, height, pixels)
    out = [0] * (4 * width * height)
    row = 2 * width

    def all_eq(value: int, *others: int) -> bool:
        return all(value == other for other in others)

    def none_eq(value: int, *others: int) -> bool:
        return all(value != other for other in others)

    for y in range(height):
        for x in range(width):
            A, B, C = at(x - 1, y - 1), at(x, y - 1), at(x + 1, y - 1)
            D, E, F = at(x - 1, y), at(x, y), at(x + 1, y)
            G, H, I = at(x - 1, y + 1), at(x, y + 1), at(x + 1, y + 1)
            Q, R = at(x - 2, y), at(x + 2, y)
            J = K = L = M = E
            if not all_eq(E, A, B, C, D, F, G, H, I):
                P, S = at(x, y - 2), at(x, y + 2)
                Bl, Dl, El, Fl, Hl = luma[B], luma[D], luma[E], luma[F], luma[H]
                # 1:1 slope rules
                if D == B and D != H and D != F and (El >= Dl or E == A) and E in (A, C, G) and (El < Dl or A != D or E != P or E != Q):
                    J = D
                if B == F and B != D and B != H and (El >= Bl or E == C) and E in (A, C, I) and (El < Bl or C != B or E != P or E != R):
                    K = B
                if H == D and H != F and H != B and (El >= Hl or E == G) and E in (A, G, I) and (El < Hl or G != H or E != S or E != Q):
                    L = H
                if F == H and F != B and F != D and (El >= Fl or E == I) and E in (C, G, I) and (El < Fl or I != H or E != R or E != S):
                    M = F
                # Intersection rules
                if E != F and all_eq(E, C, I, D, Q) and all_eq(F, B, H) and F != at(x + 3, y):
                    K = M = F
                if E != D and all_eq(E, A, G, F, R) and all_eq(D, B, H) and D != at(x - 3, y):
                    J = L = D
                if E != H and all_eq(E, G, I, B, P) and all_eq(H, D, F) and H != at(x, y + 3):
                    L = M = H
                if E != B and all_eq(E, A, C, H, S) and all_eq(B, D, F) and B != at(x, y - 3):
                    J = K = B
                if Bl < El and all_eq(E, G, H, I, S) and none_eq(E, A, D, C, F):
                    J = K = B
                if Hl < El and all_eq(E, A, B, C, P) and none_eq(E, D, G, I, F):
                    L = M = H
                if Fl < El and all_eq(E, A, D, G, Q) and none_eq(E, B, C, I, H):
                    K = M = F
                if Dl < El and all_eq(E, C, F, I, R) and none_eq(E, B, A, G, H):
                    J = L = D
                # 2:1 slope rules
                if H != B:
                    if H not in (A, E, C):
                        if all_eq(H, G, F, R) and none_eq(H, D, at(x + 2, y - 1)):
                            L = M
                        if all_eq(H, I, D, Q) and none_eq(H, F, at(x - 2, y - 1)):
                            M = L
                    if B not in (I, G, E):
                        if all_eq(B, A, F, R) and none_eq(B, D, at(x + 2, y + 1)):
                            J = K
                        if all_eq(B, C, D, Q) and none_eq(B, F, at(x - 2, y + 1)):
                            K = J
                if F != D:
                    if D not in (I, E, C):
                        if all_eq(D, A, H, S) and none_eq(D, B, at(x + 1, y + 2)):
                            J = L
                        if all_eq(D, G, B, P) and none_eq(D, H, at(x + 1, y - 2)):
                            L = J
                    if F not in (E, A, G):
                        if all_eq(F, C, H, S) and none_eq(F, B, at(x - 1, y + 2)):
                            K = M
                        if all_eq(F, I, B, P) and none_eq(F, H, at(x - 1, y - 2)):
                            M = K
            top, bottom = 2 * y * row + 2 * x, (2 * y + 1) * row + 2 * x
            out[top], out[top + 1], out[bottom], out[bottom + 1] = J, K, L, M
    return 2 * width, 2 * height, out


def luma_of(palette: list[tuple[int, int, int, int]]) -> list[int]:
    """MMPX luma: brighter is larger, transparent sorts after everything opaque."""
    return [(r + g + b + 1) * (256 - a) for r, g, b, a in palette] + [0] * (256 - len(palette))


def magnify(index_image: Image.Image, palette: list[tuple[int, int, int, int]], factor: int,
            method: str = "mmpx") -> Image.Image:
    """Magnify an 'L' index image by 2, 4 or 8 with the chosen index-only filter."""
    if index_image.mode != "L" or factor not in (2, 4, 8):
        raise ValueError("expects an 'L' index image and a power-of-two factor up to 8")
    width, height = index_image.size
    pixels = list(index_image.getdata())
    luma = luma_of(palette)
    while factor > 1:
        if method == "mmpx":
            width, height, pixels = mmpx2x(width, height, pixels, luma)
        elif method == "scale2x":
            width, height, pixels = scale2x(width, height, pixels)
        else:
            raise ValueError(f"unknown method {method}")
        factor //= 2
    out = Image.new("L", (width, height))
    out.putdata(pixels)
    return out
