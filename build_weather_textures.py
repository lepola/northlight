"""Procedural weather textures for the art layer, generated at build time (no client bytes).

# 0.3.198 (rain): replaces the client's rain streaks and snow flake with BLP2 in the client's own
palettized layout (encoding 1, alpha depth 8; the 3.3.5a client never uses encoding 3), full mip chain. The aspect ratios are the runtime's identity signature:
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


def write_blp_paletted_alpha(width, height, levels):
    """A BLP2 in the layout the 3.3.5a client reads (encoding 1, alpha depth 8, alpha type 8: the client's stock files
    are palettized or DXT, never encoding 3) from `levels`: BGRA bytes of each mip, largest first, down to 1x1, of
    one constant colour. Palette: 256 BGRA entries, entry 0 = the colour, the rest zero; then per mip `w*h` palette
    indices (all 0) followed by `w*h` alpha bytes, which carry the shape."""
    colour = levels[0][:3]
    assert all(level[i:i+3] == colour for level in levels for i in range(0, len(level), 4)), 'one constant colour'
    offsets, sizes, data, w, h = [0]*16, [0]*16, bytearray(colour+b'\xff'+bytes(1020)), width, height
    for i, level in enumerate(levels):
        assert len(level) == w*h*4 and i < 16
        offsets[i], sizes[i] = HEADER+len(data), 2*w*h
        data.extend(bytes(w*h))
        data.extend(level[3::4])
        w, h = max(1, w//2), max(1, h//2)
    assert (w, h) == (1, 1) and len(levels) == len(list(mip_sizes(width, height)))
    return struct.pack('<4sI4B2I32I', b'BLP2', 1, 1, 8, 8, 1, width, height, *offsets, *sizes)+data


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


STREAK_SIGMA_PX, STREAK_FADE = .62, .08   # thin crisp core ~1.5 px wide (FWHM) at 32 px (0.3.199 game tests: 1.1 ~2.6 px, .9, .75); the streak fades only over the last 8% at each end
# 0.3.199: the streak is centred on a texel (16 of 0..31), not on the 15|16 border: a border centre always covers two equal texels, so no
# sigma could make the core narrower than 2 px (game test: 'thinner'); on a texel centre the core is one texel with soft neighbours.
STREAK_MIN_SIGMA_TEXELS = .45                # no mip level narrows the core below this (0.3.199: was .75, which widened the far drops)
STREAK_KEEP_PEAK_WIDTH = 4                   # levels at least this wide keep the full alpha peak on their own middle texel


def streak_x(u, sigma, centre=.5):
    return math.exp(-((u-centre)/sigma)**2/2)


def streak_y(v):
    return smoothstep(0, STREAK_FADE, v)*smoothstep(1, 1-STREAK_FADE, v)


def texel_average(fn, count, samples=16):
    """Mean of fn over each of `count` equal cells of 0..1, `samples` points per cell."""
    return [sum(fn((i+(k+.5)/samples)/count) for k in range(samples))/samples for i in range(count)]


def streak_chain(width, height, colour, alpha_max):
    """Mip levels (BGRA bytes) of the streak, each computed from the analytic shape (the area average of the same
    line over every texel of the level) instead of box-filtering 8-bit texels. A box filter keeps the mean alpha, so
    the peak falls to a third by level 2 and ~0.06 at 1 px wide, and 8-bit rounding drops the thin tails to 0: far
    rain breaks into faint dots. 0.3.199 (game tests: far drops read wide and see-through): the game picks the mip by
    the streak's length (1:16 texture), so most drops use the 8..16-texel-wide levels. Down to STREAK_KEEP_PEAK_WIDTH
    texels every level is centred on its own middle texel, keeps a one-texel core (across-sigma >= STREAK_MIN_SIGMA_TEXELS)
    and the full alpha_max peak, like the stock texture's opaque core; narrower levels (far, tiny drops) keep the
    mean alpha of the last full-peak level (~.23, the stock texture's far mips are ~.21): keeping the peak there would
    make a 1 px level a ~0.57 mean slab. The line is separable (across x along),
    so the texel mean is the product of the 1-D means."""
    def across(w):
        centre = (w//2+.5)/w if w >= STREAK_KEEP_PEAK_WIDTH else (width//2+.5)/width
        sigma = STREAK_SIGMA_PX/width if w == width else max(STREAK_SIGMA_PX/width, STREAK_MIN_SIGMA_TEXELS/w)
        return texel_average(lambda u: streak_x(u, sigma, centre), w)
    ay0 = max(texel_average(streak_y, height))
    kept = None   # mean alpha (across) of the last full-peak level, the reference of the narrower ones
    b, g, r = [round(c*255) for c in colour[::-1]]
    levels, w, h = [], width, height
    while True:
        ax = across(w)
        if w >= STREAK_KEEP_PEAK_WIDTH or kept is None:
            gain = alpha_max/(max(ax)*ay0); kept = sum(ax)*gain/w
        else:
            gain = min(alpha_max/(max(ax)*ay0), kept/(sum(ax)/w))   # keep the last full-peak level's mean (the stock far drops: ~.21)
        ay = texel_average(streak_y, h)
        out = bytearray()
        for y in range(h):
            for x in range(w):
                out += bytes((b, g, r, min(255, round(ax[x]*ay[y]*gain*255))))
        levels.append(bytes(out))
        if (w, h) == (1, 1): return levels
        w, h = max(1, w//2), max(1, h//2)


def flake(u, v):
    # Round in uv space: the 1:2 texture (32x64) is stretched over a square quad, so a circle of radius .4 in
    # uv is round on screen (and a 2:1 ellipse in texel space). 1:2 costs at most one mip level of horizontal
    # sharpness (the mip follows the larger derivative); 1:32 would collapse to a 1x16 bar by ~16 px. Bright core, soft edge.
    r = math.hypot(u-.5, v-.5)/.4
    return (1-smoothstep(0, 1, r))**1.5


RAIN, RAIN_RED, SNOW = (.90, .90, .90), (.75, .22, .18), (.97, .97, .97)   # Forever-style rain is near-white, not blue
TEXTURES = {   # archive name -> (width, height, colour, alpha max, mip levels builder)
    'textures\\Weather\\RainDrop01.blp': (32, 512, RAIN, .75, streak_chain),
    'Textures\\WEATHER\\RAINDROPRED01.BLP': (32, 512, RAIN_RED, .75, streak_chain),
    'textures\\Weather\\SnowFlake01.blp': (32, 64, SNOW, .8, lambda w, h, colour, alpha_max: mip_chain(w, h, image(w, h, colour, alpha_max, flake))),
}


def weather_textures():
    """{archive name: BLP2 bytes}; deterministic."""
    return {name: write_blp_paletted_alpha(w, h, levels(w, h, colour, alpha_max)) for name, (w, h, colour, alpha_max, levels) in TEXTURES.items()}


if __name__ == '__main__':
    for name, data in weather_textures().items():
        print(name, len(data))
