"""Build and run the real Northlight precipitation/lens passes without WoW (Windows)."""
import argparse
import array
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def prepare_capture(metadata_path, output):
    """Validate an on-demand weather capture and pack bounded native replay input."""
    metadata_path = Path(metadata_path).resolve()
    if metadata_path.stat().st_size > 256 * 1024:
        raise ValueError('Weather metadata exceeds 256 KiB')
    meta = json.loads(metadata_path.read_text(encoding='utf-8'))
    if not isinstance(meta, dict) or meta.get('schema') != 'northlight-weather-capture-v1':
        raise ValueError('Unsupported weather capture schema')
    if meta.get('status') != 'captured':
        raise ValueError('Weather pass was unavailable: ' + str(meta.get('reason', 'no reason recorded')))
    width, height = meta.get('width'), meta.get('height')
    if type(width) is not int or type(height) is not int or not (1 <= width <= 8192 and 1 <= height <= 8192) or width * height * 8 > 64 * 1024 * 1024:
        raise ValueError('Invalid or oversized capture dimensions')
    constants = meta.get('constants')
    if not isinstance(constants, list) or len(constants) != 10 or any(not isinstance(row, list) or len(row) != 4 for row in constants):
        raise ValueError('Expected ten weather float4 constants')
    values = [float(value) for row in constants for value in row]
    if not all(math.isfinite(value) and abs(value) < 1e38 for value in values):
        raise ValueError('Weather constants contain invalid numbers')
    if not (0 < values[2] < values[3] and values[4] != 0 and values[5] != 0 and values[9] > 0):
        raise ValueError('Invalid projection or depth range in captured weather context')

    def image(key, formats):
        name = meta.get(key)
        if not isinstance(name, str) or Path(name).name != name or '/' in name or '\\' in name:
            raise ValueError('Capture file must be a sibling basename: ' + key)
        path = metadata_path.parent / name
        if path.resolve().parent != metadata_path.parent:
            raise ValueError('Capture image must stay in the capture folder: ' + key)
        if path.stat().st_size > 64 * 1024 * 1024 + 20:
            raise ValueError('Capture image exceeds 64 MiB: ' + key)
        data = path.read_bytes()
        if len(data) < 20:
            raise ValueError('Truncated FGR: ' + key)
        magic, w, h, pixel_format, stride = struct.unpack('<5I', data[:20])
        if magic != 0x31524746 or (w, h) != (width, height) or formats.get(pixel_format) != stride or len(data) != 20 + w * h * stride:
            raise ValueError('Invalid FGR header or size: ' + key)
        return pixel_format, data[20:]

    _, depth = image('depth_file', {114: 4})
    depths = array.array('f')
    depths.frombytes(depth)
    if sys.byteorder != 'little':
        depths.byteswap()
    if any(not math.isfinite(value) or value < 0 or value > 1.00001 for value in depths):
        raise ValueError('Captured raw depth has non-finite or out-of-range samples')
    pixel_format, scene = image('scene_file', {21: 4, 22: 4, 113: 8})
    hdr = pixel_format == 113
    if hdr:
        def display(value):
            value = max(0.0, value) if math.isfinite(value) else 0.0
            value = 12.92 * value if value <= .0031308 else 1.055 * value ** (1 / 2.4) - .055
            return round(min(1.0, value) * 255)
        scene = bytes(channel for red, green, blue, alpha in struct.iter_unpack('<4e', scene)
                      for channel in (display(blue), display(green), display(red), 255))
    required = bool(meta.get('name_mask_required'))
    mask_available = bool(meta.get('name_mask_file'))
    restore = bytearray(scene)
    if mask_available:
        _, mask = image('name_mask_file', {21: 4})
        restore[3::4] = mask[2::4]  # FGR stores BGRA; the exported red channel is blocked coverage.
    else:
        restore[3::4] = bytes(width * height)
    kinds = {'none': 0, 'rain': 1, 'snow': 2, 'sand': 3}
    presets = {'Quality': 0, 'Balanced': 1, 'Performance': 2}
    if meta.get('kind') not in kinds or meta.get('preset') not in presets:
        raise ValueError('Invalid captured weather kind or preset')
    tuning = meta.get('quality', {})
    if not isinstance(tuning, dict):
        raise ValueError('Invalid captured quality settings')
    fields = [('Weather', 1, 0, 1), ('RainLens', 1, 0, 1), ('SnowLens', 1, 0, 1),
              ('RainLensStrength', 60, 0, 100), ('SnowLensStrength', 60, 0, 100),
              ('RainDropSize', 100, 25, 300), ('SnowFlakeSize', 100, 25, 300),
              ('RainLensSize', 100, 25, 300), ('SnowLensSize', 100, 25, 300), ('RainFog', 1, 0, 2),
              ('RainRivulets', 1, 0, 1), ('SnowFrost', 1, 0, 1), ('SnowFrostStrength', 70, 0, 100),
              ('SandLens', 1, 0, 1), ('SandLensStrength', 60, 0, 100), ('SandLensSize', 100, 25, 300),
              ('WeatherWindDirection', 45, 0, 359), ('WeatherWindStrength', 100, 0, 300), ('WeatherCameraMotion', 1, 0, 1)]
    settings = []
    for key, default, low, high in fields:
        value = tuning.get(key, default)
        if not isinstance(value, int) or not low <= value <= high:
            raise ValueError('Invalid captured quality value: ' + key)
        settings.append(value)
    state = [float(meta.get(key, default)) for key, default in [('intensity', 1), ('blend', 1), ('layer_gate', 1), ('heat', 0)]]
    if not all(math.isfinite(value) and 0 <= value <= 1 for value in state):
        raise ValueError('Invalid captured weather state')
    flags = int(mask_available) | int(required) << 1 | int(hdr) << 2 | int(bool(meta.get('complete'))) << 3
    flags |= int(bool(meta.get('sky_seen'))) << 4 | int(meta.get('lens_exposed') is False) << 5
    packed = struct.pack('<6I40f19I4f', 0x33504357, width, height, kinds[meta['kind']], presets[meta['preset']], flags,
                         *values, *settings, *state)
    destination = Path(output) / 'weather-capture.bin'
    destination.write_bytes(packed + scene + depth + restore)
    return destination


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--smoke', action='store_true', help='Render and validate all modes, save BMPs, then exit')
    parser.add_argument('--backend', type=Path, help='Optional x86 D3D9 backend; default is Windows system D3D9')
    parser.add_argument('--background', type=Path, help='Optional image in place of the synthetic scene (Pillow required)')
    parser.add_argument('--capture', type=Path, help='Replay a capture-N-weather.json from Diagnostics > Capture weather')
    parser.add_argument('--recompile-shaders', action='store_true', help='Regenerate production shader bytecode after editing HLSL')
    args = parser.parse_args()
    if args.capture and args.background:
        parser.error('Use either a captured frame or a custom background.')
    if os.name != 'nt':
        parser.error('The interactive lab requires Windows; pure weather tests remain portable.')
    output = fp.out() / 'weather-lab'
    output.mkdir(parents=True, exist_ok=True)
    if args.recompile_shaders:
        subprocess.run([sys.executable, str(fp.SCRIPTS / 'shaders/compile_surface_shaders.py'), '--compatible-compiler'], check=True)
    imgui = ['imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui_impl_dx9.cpp']
    exe = output / 'weather-lab.exe'
    (output / 'temp').mkdir(exist_ok=True)
    env = dict(fp.zig_env(), ZIG_GLOBAL_CACHE_DIR=str(output / 'zig-cache'),
               TEMP=str(output / 'temp'), TMP=str(output / 'temp'))
    # The lab shares production headers and bytecode, never loads the proxy or game DLLs.
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                    *fp.include_flags(), str(fp.SCRIPTS / 'weather_lab.cpp'),
                    *(str(fp.src(name)) for name in imgui), '-luser32', '-lgdi32', '-o', str(exe)],
                   cwd=output, env=env, check=True)
    if args.build_only:
        print(exe)
        return
    command = [str(exe)]
    if args.backend:
        command += ['--backend', str(args.backend.resolve())]
    if args.background:
        from PIL import Image
        with Image.open(args.background) as image:
            image.convert('RGB').resize((960, 640), Image.Resampling.LANCZOS).save(output / 'background.bmp')
        command += ['--background', str(output / 'background.bmp')]
    if args.capture:
        try:
            command += ['--capture-bin', str(prepare_capture(args.capture, output))]
        except (OSError, ValueError, TypeError, struct.error) as error:
            parser.error(str(error))
    if args.smoke:
        command += ['--smoke']
    print('Weather lab:', output, flush=True)
    subprocess.run(command, cwd=output, check=True)


if __name__ == '__main__':
    main()
