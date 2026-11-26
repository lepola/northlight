#!/usr/bin/env python3
"""Pictures of the in-game settings window without starting the game.

    python3 scripts/dev_tuning/settings_menu_preview.py                      # every page, into out/settings-menu-preview
    python3 scripts/dev_tuning/settings_menu_preview.py --height 1440        # at the 1440p UI scale
    python3 scripts/dev_tuning/settings_menu_preview.py --out DIR click=Water scroll=6 shot=water-lower
    python3 scripts/dev_tuning/settings_menu_preview.py --no-tideglass       # as installed without Tideglass

Builds the real window (src/overlay/settings_panel.cpp) natively against the production settings service
and draws it in software, so a change to the menu can be looked at in seconds and without a GPU. The
settings come from a scratch copy of the shipped INI files; nothing in a game folder is read or written.
It shows layout, wording and how controls respond. Fonts and colours match the game; blending over a real
scene, input routing and Direct3D do not exist here and still need a look in game.

Steps after the options run in order: click=<label> clicknear=<label>:<dx>,<dy> (a control without a label,
by its offset from one that has) rclicknear=<label>:<dx>,<dy> (the same with the right button)
drag=<label>:<dx>,<dy> dragnear=<label>:<x>,<y>,<dx>,<dy> (press at an offset, release after moving)
altdragnear=... (the same with Alt held) type=<text> scroll=<notches> wait=<frames> shot=<name>.
Needs clang++; Pillow converts the pictures to PNG when it is installed.
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
import northlight_paths as fp  # noqa: E402

HERE = Path(__file__).resolve().parent
PAGES = ['Overview', 'Lighting & AO', 'Shadows', 'Materials & parallax', 'Atmosphere & weather', 'Spells & names', 'Water',
         'Regional appearance', 'Colour grading', 'Sun & moon', 'Shadow range', 'Renderer & HDR', 'Performance', 'Diagnostics']
IMGUI = ['imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp']


def slug(title):
    return ''.join(c if c.isalnum() else '-' for c in title.lower()).replace('---', '-')


def build(work):
    exe = work / 'settings_menu_preview'
    flags = ['-std=c++17', '-O2', '-w', *fp.include_flags(), '-DIMGUI_USER_CONFIG="northlight_imconfig.h"', '-DIMGUI_ENABLE_TEST_ENGINE']
    sources = [HERE / 'settings_menu_preview.cpp', fp.src('settings_panel.cpp'), *[fp.src(name) for name in IMGUI]]
    objects = []
    jobs = []
    for source in sources:
        obj = work / (source.stem + '.o')
        objects.append(obj)
        jobs.append(subprocess.Popen(['clang++', *flags, '-c', str(source), '-o', str(obj)]))
    if any(job.wait() for job in jobs):
        sys.exit('the preview did not build')
    subprocess.run(['clang++', *map(str, objects), '-pthread', '-o', str(exe)], check=True)
    return exe


def seed(config, tideglass=True):
    """The INI files a fresh combined install has, plus the marker that makes the Water page appear; without
    Tideglass, what the installer's --no-tideglass leaves: no core and no water settings."""
    config.mkdir()
    shutil.copy(fp.RENDERER / 'windows-package' / 'northlight-quality.ini', config)
    for name in ('celestial-profiles.ini', 'shadow-range-profiles.ini', *(('water.ini', 'water-host.ini') if tideglass else ())):
        shutil.copy(fp.REPO / 'client-config' / name, config)
    shutil.copytree(fp.REPO / 'client-config' / 'appearance-presets', config / 'appearance-presets')
    if tideglass:
        (config / 'water-core.dll').write_bytes(b'')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', type=Path, default=fp.out() / 'settings-menu-preview')
    parser.add_argument('--height', type=int, default=1080, help='back buffer height the UI scale is taken from')
    parser.add_argument('--no-tideglass', action='store_true', help='the window as it is with Tideglass left out')
    parser.add_argument('steps', nargs='*')
    args = parser.parse_args()
    pages = [title for title in PAGES if title != 'Water' or not args.no_tideglass]
    steps = args.steps or [step for title in pages for step in (f'click={title}', f'shot={slug(title)}')]
    args.out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='northlight-menu-preview-') as tmp:
        work = Path(tmp)
        exe = build(work)
        seed(work / 'config', not args.no_tideglass)
        result = subprocess.run([str(exe), str(work / 'config'), str(args.out), str(args.height), *steps])
    try:
        from PIL import Image
    except ImportError:
        Image = None
    for ppm in sorted(args.out.glob('*.ppm')):
        if Image:
            Image.open(ppm).save(ppm.with_suffix('.png'))
            ppm.unlink()
        print(ppm.with_suffix('.png' if Image else '.ppm'))
    return result.returncode


if __name__ == '__main__':
    sys.exit(main())
