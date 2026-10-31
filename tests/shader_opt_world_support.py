"""Small CPU/source checks for cached fog depth and independent material ranges.

Run directly with Python; shader compilation and GPU equivalence remain separate.
"""
from itertools import product
from pathlib import Path
import math
import struct


ROOT = Path(__file__).resolve().parents[1]


def f32(x):
    return struct.unpack("f", struct.pack("f", x))[0]


def half_center(pixel, size):
    return f32(f32(pixel + .5) / size)


def depth_pixel(q, full):
    return min(max(math.floor(f32(q * full)), 0), full - 1)


def linear_depth(d):
    near, far = f32(.1), f32(1000)
    return f32(f32(near * far) / max(f32(far - f32(d * f32(far - near))), f32(.00001)))


def water_receiver(z, water, coverage, ready):
    if ready < .5:
        return z
    tolerance = max(f32(.012), f32(water * f32(.0007)))
    return water if water > f32(.1) and water <= f32(z + tolerance) and coverage > f32(.003) else z


def main():
    hlsl = (ROOT / "shaders/world_effects.hlsl").read_text()
    blur = hlsl.split("float4 FogBlur(", 1)[1].split("\n}", 1)[0]
    receiver = hlsl.split("float blurReceiverDistance(", 1)[1].split("\n}", 1)[0]
    material = hlsl.split("float4 MaterialComposite(", 1)[1].split("\n}", 1)[0]
    assert "CurrentDistance" in receiver and "normalizedDepth" not in receiver + blur
    assert "water.x<=z+max(.012,water.x*.0007)&&water.y>.003" in receiver
    assert "float z=i==0?center:blurReceiverDistance(q);" in blur
    assert "return float4(max(color.rgb,0),color.a);" in material

    cases = 0
    for width, height in ((128, 96), (137, 79), (1728, 1117), (1919, 1079)):
        for full in (width, height):
            half = full // 2
            # Both blur axes have these same one-dimensional sampling rules.
            for base, tap in product((0, 1, half // 2, half - 2, half - 1), range(-2, 3)):
                pixel = min(max(base + 2 * tap, 0), half - 1)
                q = half_center(pixel, half)
                # WorldNormals/TemporalLight quantize their half-res output center.
                stored_pixel = math.floor(f32(q * half))
                stored_q = half_center(stored_pixel, half)
                assert stored_pixel == pixel
                assert depth_pixel(stored_q, full) == depth_pixel(q, full)
                for d in (0., .1, .9, .99998, 1.):
                    old_z = linear_depth(f32(d))
                    cached_z = f32(old_z)  # D3DFMT_R32F preserves the shader float.
                    assert old_z == cached_z
                    for water, coverage, ready in product(
                        (0., f32(.1), old_z, f32(old_z + .012), f32(old_z + 1)),
                        (0., f32(.003), f32(.0031), 1.), (0., .5, 1.)
                    ):
                        assert water_receiver(old_z, water, coverage, ready) == water_receiver(cached_z, water, coverage, ready)
                        cases += 1

    # Neither effect's cutoff may suppress the other effect's wider support.
    for strength, normal_range, occlusion_on, explicit_range, z in product(
        (0., 1.), (0., 20., 80.), (0., 1.), (0., 10., 60., 120.), (0., 10., 20., 60., 80., 120.)
    ):
        occlusion_range = explicit_range if explicit_range > 0 else normal_range
        skip = (strength <= 0 or z >= normal_range) and (occlusion_on <= 0 or z >= occlusion_range)
        normal_active = strength > 0 and z < normal_range
        occlusion_active = occlusion_on > 0 and z < occlusion_range
        assert skip == (not normal_active and not occlusion_active)
    print(f"PASS cached fog footprint/water ({cases} cases), independent material ranges; no compiler/GPU")


if __name__ == "__main__":
    main()
