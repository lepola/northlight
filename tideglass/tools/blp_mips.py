"""Complete a missing 1x1 BC3 mip without recompressing existing texture levels."""
import struct


def _round_ratio(total, count):
    return (total + count // 2) // count


def _bc3_pixels(block):
    a, b = block[:2]
    alpha = [a, b]
    if a > b:
        alpha += [(a * (7 - i) + b * i) // 7 for i in range(1, 7)]
    else:
        alpha += [(a * (5 - i) + b * i) // 5 for i in range(1, 5)] + [0, 255]
    alpha_bits = int.from_bytes(block[2:8], 'little')
    first, second, colour_bits = struct.unpack_from('<HHI', block, 8)

    def rgb(value):
        r, g, b = value >> 11, (value >> 5) & 63, value & 31
        return (r << 3 | r >> 2, g << 2 | g >> 4, b << 3 | b >> 2)

    x, y = rgb(first), rgb(second)
    colours = [x, y, tuple((2 * x[i] + y[i]) // 3 for i in range(3)),
               tuple((x[i] + 2 * y[i]) // 3 for i in range(3))]
    return [(*colours[(colour_bits >> (2 * i)) & 3], alpha[(alpha_bits >> (3 * i)) & 7])
            for i in range(16)]


def complete_terminal_bc3_mip(raw):
    """Return original bytes or append an alpha-weighted, box-filtered terminal mip.

    Only contiguous BLP2 BC3 chains missing their final 1x1 level are repaired.
    Other missing data is rejected rather than guessed. Existing compressed levels
    and all their pixels remain byte-exact. Complete textures remain byte-exact.
    """
    if len(raw) < 148 or raw[:4] != b'BLP2':
        raise ValueError('Expected a BLP2 header')
    if struct.unpack_from('<I', raw, 4)[0] != 1 or raw[8:12] != bytes((2, 8, 7, 1)):
        raise ValueError('Expected mipmapped BLP2 BC3 with 8-bit alpha')
    width, height = struct.unpack_from('<II', raw, 12)
    if any(n < 1 or n > 16384 or n & (n - 1) for n in (width, height)):
        raise ValueError('Expected power-of-two dimensions up to 16384')
    levels = max(width, height).bit_length()
    offsets, sizes = (struct.unpack_from('<16I', raw, at) for at in (20, 84))
    missing, spans = [], []
    for i in range(levels):
        if not sizes[i]:
            missing.append(i)
            continue
        w, h = max(1, width >> i), max(1, height >> i)
        expected = ((w + 3) // 4) * ((h + 3) // 4) * 16
        if sizes[i] != expected or offsets[i] < 148 or offsets[i] + sizes[i] > len(raw):
            raise ValueError('Invalid BC3 mip size or span')
        span = offsets[i], offsets[i] + sizes[i]
        if any(span[0] < end and start < span[1] for start, end in spans):
            raise ValueError('Overlapping mip spans')
        spans.append(span)
    if not missing:
        return raw
    if missing != [levels - 1] or levels == 1 or any(sizes[levels:]):
        raise ValueError('Only a missing terminal 1x1 mip can be repaired')
    previous = levels - 2
    w, h = max(1, width >> previous), max(1, height >> previous)
    pixels = _bc3_pixels(raw[offsets[previous]:offsets[previous] + 16])
    valid = [pixels[y * 4 + x] for y in range(h) for x in range(w)]
    alpha_sum = sum(p[3] for p in valid)
    alpha = _round_ratio(alpha_sum, len(valid))
    colour = [_round_ratio(sum(p[c] * p[3] for p in valid), alpha_sum) if alpha_sum else 0
              for c in range(3)]
    r, g, b = (_round_ratio(colour[i] * maximum, 255) for i, maximum in enumerate((31, 63, 31)))
    rgb565 = r << 11 | g << 5 | b
    terminal = bytes((alpha, alpha)) + bytes(6) + struct.pack('<HHI', rgb565, rgb565, 0)
    result = bytearray(raw)
    struct.pack_into('<I', result, 20 + 4 * (levels - 1), len(result))
    struct.pack_into('<I', result, 84 + 4 * (levels - 1), len(terminal))
    for i in range(levels, 16):
        struct.pack_into('<I', result, 20 + 4 * i, 0)
        struct.pack_into('<I', result, 84 + 4 * i, 0)
    result.extend(terminal)
    return bytes(result)
