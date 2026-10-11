# Builds hud_raindrops.dds, the drop texture of the HUD raindrops (Shaders/Includes/HudRaindrops.hlsl):
# a 512 x 512 tile of random water drops, wrapping, uncompressed A8R8G8B8 with its mipmaps.
#   r  the drop's height (1 in the middle, 0 at the rim and outside); the shader shrinks each drop
#      toward its middle over its life by subtracting a growing threshold from this
#   g  the drop's slope along u, b along v (0.5 flat): its normal's tangent-space x and y
#   a  the drop's time offset (one random value over the whole drop), so drops live out of step
# The layout follows Screen Space Shaders' hud_rain.dds (STALKER Anomaly, Ascii1457); the texture is
# made here, not copied. Run with Python 3: python make_hud_raindrops.py
import random, struct, math, os

SIZE = 512
DROPS = 950
random.seed(1457)

height = [0.0] * (SIZE * SIZE)
slope_u = [0.0] * (SIZE * SIZE)
slope_v = [0.0] * (SIZE * SIZE)
phase = [0.0] * (SIZE * SIZE)

for _ in range(DROPS):
    cx, cy = random.uniform(0, SIZE), random.uniform(0, SIZE)
    radius = 2.5 + 10.0 * random.random() ** 2.2          # mostly small, a few big
    amplitude = random.uniform(0.55, 1.0)
    offset = random.random()
    stretch = random.uniform(1.0, 1.35)                    # a little longer downhill (v), as drops sag
    r = int(math.ceil(radius * stretch)) + 1
    for y in range(int(cy) - r, int(cy) + r + 1):
        for x in range(int(cx) - r, int(cx) + r + 1):
            du = (x + 0.5 - cx) / radius
            dv = (y + 0.5 - cy) / (radius * stretch)
            dd = du * du + dv * dv
            if dd >= 1.0:
                continue
            h = amplitude * math.sqrt(1.0 - dd)
            i = (y % SIZE) * SIZE + (x % SIZE)
            if h <= height[i]:
                continue
            height[i] = h
            # A dome's normal leans outward from its middle, more toward the rim.
            slope_u[i] = du
            slope_v[i] = dv
            phase[i] = offset

def to_byte(v):
    return max(0, min(255, int(round(v * 255.0))))

def level(size, h, su, sv, ph):
    out = bytearray(size * size * 4)
    for i in range(size * size):
        out[i * 4 + 0] = to_byte(sv[i] * 0.5 + 0.5)       # B: slope along v
        out[i * 4 + 1] = to_byte(su[i] * 0.5 + 0.5)       # G: slope along u
        out[i * 4 + 2] = to_byte(h[i])                     # R: height
        out[i * 4 + 3] = to_byte(ph[i])                    # A: time offset
    return out

def down(size, *planes):
    half = size // 2
    result = []
    for p in planes:
        q = [0.0] * (half * half)
        for y in range(half):
            for x in range(half):
                q[y * half + x] = 0.25 * (p[(2 * y) * size + 2 * x] + p[(2 * y) * size + 2 * x + 1] + p[(2 * y + 1) * size + 2 * x] + p[(2 * y + 1) * size + 2 * x + 1])
        result.append(q)
    return result

levels = []
size, planes = SIZE, [height, slope_u, slope_v, phase]
while True:
    levels.append(level(size, *planes))
    if size == 1:
        break
    planes = down(size, *planes)
    size //= 2

header = struct.pack("<4sIIIIIII", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000, SIZE, SIZE, SIZE * 4, 0, len(levels))
header += b"\0" * 44
header += struct.pack("<IIIIIIII", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
header += struct.pack("<IIIII", 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
assert len(header) == 128
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "hud_raindrops.dds")
with open(path, "wb") as f:
    f.write(header)
    for l in levels:
        f.write(l)
print("wrote", path, len(levels), "levels")
