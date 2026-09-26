"""Generates resource/Textures/Water/NVR_WaterWaves.dds, the animated wave field Complex Water
samples (Includes/ComplexWater.hlsl, "Waves").

A square patch of wind-driven water, worked out the way film and game oceans are (Tessendorf,
"Simulating Ocean Water"): a wind-wave spectrum (Phillips) filled with random waves, each moving at
the speed a real water wave of its length does, summed with an inverse FFT. The crests are then
pulled together by the horizontal motion of the water (choppy waves), so they come sharp and the
troughs broad, and the height is resampled back onto the regular grid. Where the crests pull
together the most, the surface folds: that is where whitecaps break.

The patch tiles in both directions, and in time: every wave's frequency is rounded to a whole
number of cycles over the loop, so the last frame runs straight into the first.

Output: a DX9 volume texture, A8R8G8B8, WAVE_SIZE x WAVE_SIZE x FRAMES with a full mip chain.
    R, G  slope along x and y (dh/dx, dh/dy), 0.5 = flat, scaled to their 99.9th percentile
    B     height, 0.5 = still water (the mean), scaled to its 99.9th percentile
    A     crest folding (whitecap foam), 0 none to 1 folded
x runs along the texture's u (columns), y along v (rows); the wind blows along +x.

The constants the shader needs are printed at the end; they go into ComplexWater.hlsl.

Usage: python tools/water/generate_water_waves.py [--preview DIR]
Needs numpy (and Pillow for --preview).
"""
import argparse
import os
import struct

import numpy as np

WAVE_SIZE = 128        # texels across the patch
FRAMES = 192           # frames over the loop: enough that blending between them keeps 96% of the slopes
PATCH = 15.0           # metres across the patch
PERIOD = 12.0          # seconds the loop lasts
PEAK_WAVELENGTH = 5.0  # metres: the spectrum's peak (a light breeze over a lake)
MIN_WAVELENGTH = 0.6   # metres: shorter waves are left to the detail normal map
GRAVITY = 9.81
SEED = 1234
TARGET_MIN_JACOBIAN = 0.15  # how far the choppiness pulls the crests together (1 none, 0 folds)

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "resource", "Textures", "Water", "NVR_WaterWaves.dds")


def spectrum():
    n = WAVE_SIZE
    k1 = 2.0 * np.pi * np.fft.fftfreq(n, d=PATCH / n)
    kx, ky = np.meshgrid(k1, k1)            # kx varies along columns (x), ky along rows (y)
    k = np.sqrt(kx * kx + ky * ky)
    k_safe = np.where(k == 0.0, 1.0, k)

    # Phillips spectrum. Its peak lies at k = 1 / (sqrt(2) L), L = V^2 / g.
    k_peak = 2.0 * np.pi / PEAK_WAVELENGTH
    big_l = 1.0 / (np.sqrt(2.0) * k_peak)
    phillips = np.exp(-1.0 / (k_safe * big_l) ** 2) / k_safe ** 4

    # Direction: along the wind, spread as cos^8 (half the energy within about 30 degrees of it), so
    # the crests run in long rows across the wind; next to nothing against it. Waves meeting their
    # like from the other way stand -- the surface bobs up and down in place like jelly instead of
    # travelling -- which an earlier 22% against the wind (and 8% every way) did.
    c = kx / k_safe
    along = np.where(c >= 0.0, c ** 8, 0.01 * c ** 8)
    phillips *= 0.99 * along + 0.01

    # Leave the waves shorter than MIN_WAVELENGTH out, smoothly.
    k_cut = 2.0 * np.pi / MIN_WAVELENGTH
    phillips *= np.exp(-(k / k_cut) ** 2)
    phillips[k == 0.0] = 0.0

    rng = np.random.default_rng(SEED)
    xi = rng.standard_normal((n, n)) + 1j * rng.standard_normal((n, n))
    h0 = xi * np.sqrt(phillips / 2.0)

    # Each wave's frequency (deep water: w^2 = g k), rounded to whole cycles over the loop.
    step = 2.0 * np.pi / PERIOD
    omega = np.sqrt(GRAVITY * k)
    omega = np.where(k > 0.0, np.maximum(np.round(omega / step), 1.0) * step, 0.0)

    # h0(-k): the grid index of -k is (-i) mod n.
    idx = (-np.arange(n)) % n
    h0_neg_conj = np.conj(h0[np.ix_(idx, idx)])
    return kx, ky, k_safe, h0, h0_neg_conj, omega


def frame_fields(kx, ky, k, h0, h0_neg_conj, omega, t):
    h = h0 * np.exp(1j * omega * t) + h0_neg_conj * np.exp(-1j * omega * t)
    height = np.fft.ifft2(h).real
    # Horizontal motion of the water, D = sum -i (k / |k|) h e^(ik.x), and its derivatives.
    dx = np.fft.ifft2(-1j * kx / k * h).real
    dy = np.fft.ifft2(-1j * ky / k * h).real
    dxx = np.fft.ifft2(kx * kx / k * h).real
    dyy = np.fft.ifft2(ky * ky / k * h).real
    dxy = np.fft.ifft2(kx * ky / k * h).real
    return height, dx, dy, dxx, dyy, dxy


def bilinear_periodic(field, px, py):
    """field sampled at fractional texel positions (px along columns, py along rows), wrapping."""
    n = field.shape[0]
    x0 = np.floor(px).astype(int)
    y0 = np.floor(py).astype(int)
    fx = px - x0
    fy = py - y0
    x0 %= n
    y0 %= n
    x1 = (x0 + 1) % n
    y1 = (y0 + 1) % n
    return ((field[y0, x0] * (1 - fx) + field[y0, x1] * fx) * (1 - fy)
            + (field[y1, x0] * (1 - fx) + field[y1, x1] * fx) * fy)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--preview", help="write PNG previews of a few frames to this folder")
    args = parser.parse_args()

    n = WAVE_SIZE
    kx, ky, k, h0, h0_neg_conj, omega = spectrum()
    times = np.arange(FRAMES) * PERIOD / FRAMES
    raw = [frame_fields(kx, ky, k, h0, h0_neg_conj, omega, t) for t in times]

    # Choppiness: the water point at x0 moves to x0 - lam * D(x0). The crests pull together as long
    # as the surface does not fold over (Jacobian > 0); lam is set so its lowest is TARGET_MIN_JACOBIAN.
    def min_jacobian(lam):
        worst = 1.0
        for (_, _, _, dxx, dyy, dxy) in raw:
            j = (1 - lam * dxx) * (1 - lam * dyy) - (lam * dxy) ** 2
            worst = min(worst, j.min())
        return worst

    lo, hi = 0.0, 1.0
    while min_jacobian(hi) > TARGET_MIN_JACOBIAN:
        hi *= 2.0
    for _ in range(40):
        mid = 0.5 * (lo + hi)
        if min_jacobian(mid) > TARGET_MIN_JACOBIAN:
            lo = mid
        else:
            hi = mid
    lam = lo

    texel = PATCH / n
    gx, gy = np.meshgrid(np.arange(n, dtype=float), np.arange(n, dtype=float))
    heights, folds = [], []
    for (height, dx, dy, dxx, dyy, dxy) in raw:
        # The height at each grid point X: the water point x0 that moved there, x0 = X + lam D(x0).
        px, py = gx.copy(), gy.copy()
        for _ in range(6):
            px, py = (gx + lam * bilinear_periodic(dx, px, py) / texel,
                      gy + lam * bilinear_periodic(dy, px, py) / texel)
        heights.append(bilinear_periodic(height, px, py))
        jac = (1 - lam * dxx) * (1 - lam * dyy) - (lam * dxy) ** 2
        folds.append(bilinear_periodic(jac, px, py))

    heights = np.array(heights)                 # frames, rows (y), columns (x); metres
    heights -= heights.mean()                   # pulling the crests together moves the mean a little
    folds = np.array(folds)
    sigma = heights.std()
    # Slopes by central differences, wrapping (dh/dx along columns, dh/dy along rows).
    slope_x = (np.roll(heights, -1, axis=2) - np.roll(heights, 1, axis=2)) / (2 * texel)
    slope_y = (np.roll(heights, -1, axis=1) - np.roll(heights, 1, axis=1)) / (2 * texel)

    # Scaled to the 99.9th percentile, not the extreme, so the 8 bits spend their steps on the waves
    # (the rare extremes clip).
    height_max = np.percentile(np.abs(heights), 99.9)
    slope_max = max(np.percentile(np.abs(slope_x), 99.9), np.percentile(np.abs(slope_y), 99.9))
    foam = np.clip((0.75 - folds) / 0.45, 0.0, 1.0)

    def to_byte(v):
        return np.clip(np.round(v * 255.0), 0, 255).astype(np.uint8)

    r = to_byte(np.clip(slope_x / slope_max, -1.0, 1.0) * 0.5 + 0.5)
    g = to_byte(np.clip(slope_y / slope_max, -1.0, 1.0) * 0.5 + 0.5)
    b = to_byte(np.clip(heights / height_max, -1.0, 1.0) * 0.5 + 0.5)
    a = to_byte(foam)
    # A8R8G8B8 is stored B, G, R, A in memory.
    level0 = np.stack([b, g, r, a], axis=-1).astype(np.float32)   # depth, rows, cols, 4

    levels = [level0]
    cur = level0
    while cur.shape[0] > 1 or cur.shape[1] > 1:
        d, h, w, _ = cur.shape
        # Halve each dimension still above 1 (wrapping pairs; an odd depth of 3 averages to 1).
        if d > 1:
            if d % 2 == 0:
                cur = 0.5 * (cur[0::2] + cur[1::2])
            else:
                cur = cur.mean(axis=0, keepdims=True)
        if h > 1:
            cur = 0.5 * (cur[:, 0::2] + cur[:, 1::2])
        if w > 1:
            cur = 0.5 * (cur[:, :, 0::2] + cur[:, :, 1::2])
        levels.append(cur)

    header = bytearray(128)
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000 | 0x800000   # caps height width pitch pixelformat mipmapcount depth
    struct.pack_into("<4sIIIIIII", header, 0, b"DDS ", 124, flags, n, n, n * 4, FRAMES, len(levels))
    struct.pack_into("<II4sIIIII", header, 76, 32, 0x41, b"\0\0\0\0", 32,
                     0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    struct.pack_into("<II", header, 108, 0x1000 | 0x8 | 0x400000, 0x200000)   # texture complex mipmap; volume
    with open(OUT, "wb") as f:
        f.write(header)
        for lvl in levels:
            f.write(np.clip(np.round(lvl), 0, 255).astype(np.uint8).tobytes())

    print("wrote %s: %d levels, %d bytes" % (os.path.normpath(OUT), len(levels), os.path.getsize(OUT)))
    print("choppiness lambda %.4f, min Jacobian %.3f" % (lam, folds.min()))
    print("sigma %.4f m, height max %.4f m, slope max %.4f" % (sigma, height_max, slope_max))
    print("foam coverage (>0.5): %.2f%%" % (100.0 * (foam > 0.5).mean()))
    print("loop seam: height change last->first frame %.4f m (typical frame step %.4f m)" % (
        np.abs(heights[0] - heights[-1]).mean(), np.abs(heights[1] - heights[0]).mean()))
    print()
    print("#define WAVE_TEX_SIZE        %.1ff" % n)
    print("#define WAVE_TEX_PATCH       %.1ff   // metres the patch spans" % PATCH)
    print("#define WAVE_TEX_PERIOD      %.1ff   // seconds the loop lasts at that size" % PERIOD)
    print("#define WAVE_TEX_PEAK        %.4ff   // patch size over the peak wavelength" % (PATCH / PEAK_WAVELENGTH))
    print("#define WAVE_TEX_HEIGHT_MAX  %.4ff   // stored height 1 over the height's standard deviation" % (height_max / sigma))
    print("#define WAVE_TEX_SLOPE_SCALE %.4ff   // slope_max * patch / sigma" % (slope_max * PATCH / sigma))

    if args.preview:
        from PIL import Image
        os.makedirs(args.preview, exist_ok=True)
        for i in (0, FRAMES // 4, FRAMES - 1):
            Image.fromarray(b[i]).save(os.path.join(args.preview, "height_%02d.png" % i))
            Image.fromarray(a[i]).save(os.path.join(args.preview, "foam_%02d.png" % i))
            shade = np.clip(128 + 127 * (-(slope_x[i] * 0.7 + slope_y[i] * 0.7) / slope_max), 0, 255).astype(np.uint8)
            Image.fromarray(shade).save(os.path.join(args.preview, "shade_%02d.png" % i))
        tiled = np.tile(b[0], (2, 2))
        Image.fromarray(tiled).save(os.path.join(args.preview, "height_tiled.png"))


if __name__ == "__main__":
    main()
