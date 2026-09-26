"""Makes Textures\\Clouds\\NVR_CloudNoise.dds, the noise the VolumetricClouds effect builds its clouds from.

A 64x64x64 volume texture that tiles in all three directions:
    R     cloud shape: soft band-limited noise with billowing cellular (Worley) noise mixed in, the
          "Perlin-Worley" noise volumetric cloud renderers build their cloud bodies from
    G     cloud detail: finer cellular noise, for eroding the edges into wisps
    B, A  unused (0, 255)

Tiling comes for free: the smooth noise is made in the frequency domain (a Fourier series is
periodic), and the cellular noise measures distances across the wrapped edges.

Run: python tools/clouds/generate_cloud_noise.py   (numpy)
"""
import os
import struct

import numpy as np

N = 64
SEED = 1221
OUT = os.path.join(os.path.dirname(__file__), "..", "..", "resource", "Textures", "Clouds", "NVR_CloudNoise.dds")


def smooth_noise(rng, lowest, highest, slope):
    """Tiling noise with energy between two frequencies (cycles per tile), amplitude ~ f^-slope."""
    white = rng.standard_normal((N, N, N))
    spectrum = np.fft.fftn(white)
    f = np.fft.fftfreq(N) * N
    fx, fy, fz = np.meshgrid(f, f, f, indexing="ij")
    k = np.sqrt(fx * fx + fy * fy + fz * fz)
    band = np.where((k >= lowest) & (k <= highest), 1.0 / np.maximum(k, 1.0) ** slope, 0.0)
    field = np.real(np.fft.ifftn(spectrum * band))
    field -= field.min()
    return field / field.max()


def worley(rng, cells):
    """Tiling cellular noise: 1 at each cell's feature point, falling to 0 a cell away."""
    points = rng.random((cells, cells, cells, 3))
    grid = (np.arange(N) + 0.5) / N * cells
    gx, gy, gz = np.meshgrid(grid, grid, grid, indexing="ij")
    cx, cy, cz = np.floor(gx).astype(int), np.floor(gy).astype(int), np.floor(gz).astype(int)
    best = np.full((N, N, N), 10.0)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            for dz in (-1, 0, 1):
                nx, ny, nz = cx + dx, cy + dy, cz + dz
                p = points[nx % cells, ny % cells, nz % cells]
                d = np.sqrt((nx + p[..., 0] - gx) ** 2 + (ny + p[..., 1] - gy) ** 2 + (nz + p[..., 2] - gz) ** 2)
                best = np.minimum(best, d)
    return np.clip(1.0 - best, 0.0, 1.0)


def worley_fbm(rng, cells):
    return worley(rng, cells) * 0.625 + worley(rng, cells * 2) * 0.25 + worley(rng, cells * 4) * 0.125


def remap(x, lo, hi):
    return np.clip((x - lo) / np.maximum(hi - lo, 1e-6), 0.0, 1.0)


def equalize(x):
    ranks = np.empty(x.size)
    ranks[np.argsort(x, axis=None, kind="stable")] = np.arange(x.size)
    return (ranks / (x.size - 1)).reshape(x.shape)


def main():
    rng = np.random.default_rng(SEED)
    soft = smooth_noise(rng, 1.0, 12.0, 1.4)
    billow = worley_fbm(rng, 4)
    # Perlin-Worley: the soft noise, pushed up where the cellular noise billows.
    shape = remap(soft, (1.0 - billow) * 0.45, 1.0)
    # Evened out (a rank transform: the pattern stays, its values spread evenly over 0-1), so the
    # effect's coverage is the share of the layer that is cloud: at coverage c, cloud where shape > 1 - c.
    shape = equalize(shape)
    detail = worley_fbm(rng, 8)
    detail = (detail - detail.min()) / (detail.max() - detail.min())

    rgba = np.zeros((N, N, N, 4), np.float64)
    rgba[..., 0] = shape * 255.0
    rgba[..., 1] = detail * 255.0
    rgba[..., 3] = 255.0
    # Stored z-major (depth slices), as a DDS volume is: [z][y][x].
    vol = np.transpose(rgba, (2, 1, 0, 3))

    # BGRA in memory (A8R8G8B8), full mip chain.
    levels = []
    cur = vol[..., [2, 1, 0, 3]]
    while True:
        levels.append(cur)
        if cur.shape[0] == 1:
            break
        cur = 0.125 * (cur[0::2, 0::2, 0::2] + cur[1::2, 0::2, 0::2] + cur[0::2, 1::2, 0::2] + cur[0::2, 0::2, 1::2]
                       + cur[1::2, 1::2, 0::2] + cur[1::2, 0::2, 1::2] + cur[0::2, 1::2, 1::2] + cur[1::2, 1::2, 1::2])

    header = bytearray(128)
    flags = 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000 | 0x800000   # caps height width pitch pixelformat mipmapcount depth
    struct.pack_into("<4sIIIIIII", header, 0, b"DDS ", 124, flags, N, N, N * 4, N, len(levels))
    struct.pack_into("<II4sIIIII", header, 76, 32, 0x41, b"\0\0\0\0", 32,
                     0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    struct.pack_into("<II", header, 108, 0x1000 | 0x8 | 0x400000, 0x200000)   # texture complex mipmap; volume
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(header)
        for lvl in levels:
            f.write(np.clip(np.round(lvl), 0, 255).astype(np.uint8).tobytes())
    print("wrote %s: %d levels, %d bytes" % (os.path.normpath(OUT), len(levels), os.path.getsize(OUT)))
    print("shape mean %.3f, detail mean %.3f" % (shape.mean(), detail.mean()))


if __name__ == "__main__":
    main()
