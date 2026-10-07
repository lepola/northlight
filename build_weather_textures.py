"""Procedural weather textures for the art layer, generated at build time (no client bytes).

# 0.3.198 (rain): replaces the client's rain streaks and snow flake with uncompressed BLP2
(encoding 3, BGRA, full mip chain). The aspect ratios are the runtime's identity signature:
rain 1:16 (stock 1:8), snow 1:2 (stock 1:1), both at most 32 wide.
"""
import math
import struct

HEADER = 148   # BLP2 header: magic, version, 4 flag bytes, w, h, 16 offsets, 16 sizes


def write_blp_bgra(width, height, levels):
    """A BLP2 (encoding 3, 8-bit alpha) from `levels`: BGRA bytes of each mip, largest first, down to 1x1."""
    offsets, sizes, data, w, h = [0]*16, [0]*16, bytearray(), width, height
    for i, level in enumerate(levels):
        assert len(level) == w*h*4 and i < 16
        offsets[i], sizes[i] = HEADER+len(data), len(level)
        data.extend(level)
        w, h = max(1, w//2), max(1, h//2)
    assert (w, h) == (1, 1) and len(levels) == len(list(mip_sizes(width, height)))
    return struct.pack('<4sI4B2I32I', b'BLP2', 1, 3, 8, 0, 1, width, height, *offsets, *sizes)+data


def mip_sizes(width, height):
    while True:
        yield width, height
        if width == 1 and height == 1: return
        width, height = max(1, width//2), max(1, height//2)


def mip_chain(width, height, pixels):
    """Box-filtered mip levels (BGRA bytes) of a BGRA image, down to 1x1."""
    levels = [bytes(pixels)]
    w, h = width, height
    while (w, h) != (1, 1):
        nw, nh = max(1, w//2), max(1, h//2)
        sx, sy, src, out = w//nw, h//nh, levels[-1], bytearray(nw*nh*4)
        for y in range(nh):
            for x in range(nw):
                for c in range(4):
                    total = sum(src[((y*sy+j)*w+x*sx+i)*4+c] for j in range(sy) for i in range(sx))
                    out[(y*nw+x)*4+c] = (total+sx*sy//2)//(sx*sy)
        levels.append(bytes(out))
        w, h = nw, nh
    return levels


def smoothstep(lo, hi, x):
    t = min(1., max(0., (x-lo)/(hi-lo)))
    return t*t*(3-2*t)


def image(width, height, colour, alpha_max, shape):
    """Level 0 (BGRA bytes): `shape(u, v)` in 0..1 at the texel centre, scaled so its peak is alpha_max."""
    samples = [shape((x+.5)/width, (y+.5)/height) for y in range(height) for x in range(width)]
    scale = alpha_max/max(samples)
    b, g, r = [round(c*255) for c in colour[::-1]]
    out = bytearray()
    for s in samples:
        out += bytes((b, g, r, round(s*scale*255)))
    return bytes(out)


def streak(u, v):
    # Thin vertical line (gaussian sigma 1.5 px of 32) fading in and out over the first/last fifth.
    return math.exp(-((u-.5)*32)**2/(2*1.5**2))*smoothstep(0, .2, v)*smoothstep(1, .8, v)


def flake(u, v):
    # Round in uv space: the 1:2 texture (32x64) is stretched over a square quad, so a circle of radius .4 in
    # uv is round on screen (and a 2:1 ellipse in texel space). 1:2 costs at most one mip level of horizontal
    # sharpness (the mip follows the larger derivative); 1:32 would collapse to a 1x16 bar by ~16 px. Bright core, soft edge.
    r = math.hypot(u-.5, v-.5)/.4
    return (1-smoothstep(0, 1, r))**1.5


RAIN, RAIN_RED, SNOW = (.86, .90, 1.), (.75, .22, .18), (.96, .97, 1.)
TEXTURES = {   # archive name -> (width, height, colour, alpha max, shape)
    'textures\\Weather\\RainDrop01.blp': (32, 512, RAIN, .35, streak),
    'Textures\\WEATHER\\RAINDROPRED01.BLP': (32, 512, RAIN_RED, .35, streak),
    'textures\\Weather\\SnowFlake01.blp': (32, 64, SNOW, .8, flake),
}


def weather_textures():
    """{archive name: BLP2 bytes}; deterministic."""
    result = {}
    for name, (w, h, colour, alpha_max, shape) in TEXTURES.items():
        result[name] = write_blp_bgra(w, h, mip_chain(w, h, image(w, h, colour, alpha_max, shape)))
    return result


if __name__ == '__main__':
    for name, data in weather_textures().items():
        print(name, len(data))
