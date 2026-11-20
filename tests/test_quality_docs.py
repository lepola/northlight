#!/usr/bin/env python3
# northlight-test:
"""Player documentation vs the Keys[] table of quality_settings.h: the one place that checks the ini template and the
windows-package README.txt settings table against the code. Reads only the player-facing files (never the repository's
top-level readme, which is the human introduction and is edited on GitHub). Source/text audit only."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re

# Developer/diagnostic keys: still parsed and logged on the QUALITY line, absent from player guidance. The combo enables SpatialAlphaFog in its preset.
DEVELOPER_KEYS = {'SpellLayerClasses', 'SpellTrace', 'SpatialAlphaFog'}
INI_NAME, README_NAME = 'windows-package/northlight-quality.ini', 'windows-package/README.txt'

code = fp.src('quality_settings.h').read_text(encoding='utf-8')
ini = fp.src(INI_NAME).read_text(encoding='utf-8')
readme = fp.src(README_NAME).read_text(encoding='utf-8')
failures = []
def fail(file, key, msg): failures.append(f'{file}: {key}: {msg}')

# Keys[] entries: {"Name",&Settings::member,min,max,{a,b,c}}, trailing comments tolerated.
table = code[code.index('inline const Key Keys[]={'):]
table = table[:table.index('\n};')]
keys = {}
for m in re.finditer(r'\{\s*"(\w+)"\s*,\s*&Settings::(\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\}\s*\}', table):
    keys[m.group(1)] = (m.group(2), int(m.group(3)), int(m.group(4)), tuple(int(m.group(i)) for i in (5, 6, 7)))
assert len(keys) > 40 and len(keys) == table.count('&Settings::'), (len(keys), table.count('&Settings::'))

# Settings member initialisers (unsigned a=1,b=2;), comments removed.
body = code[code.index('struct Settings'):code.index('struct Key ')]
body = re.sub(r'/\*.*?\*/|//[^\n]*', '', body, flags=re.S)
defaults = {}
for st in re.finditer(r'\bunsigned\s+([^;(){}]*);', body):
    for name, val in re.findall(r'(\w+)\s*=\s*(\d+)', st.group(1)): defaults[name] = int(val)

# README.txt settings table: the consecutive rows after the "Individual settings" line.
lines = readme.split('\n')
start = next(i for i, l in enumerate(lines) if l.startswith('Individual settings')) + 1
rows = {}
for l in lines[start:]:
    if not re.match(r'^  \w', l): break  # the table ends at the first line that is not indented by two spaces
    m = re.match(r'^  (\w+) +(\d+) ?/ ?(\d+) ?/ ?(\d+)(?: |$)', l)  # MinSkinnedTriangles and ShadowDirectionSteps write "a/b/c"
    if not m: fail(README_NAME, l[:40], 'line inside the settings table is not a "  Name a / b / c" row'); continue
    rows[m.group(1)] = tuple(int(m.group(i)) for i in (2, 3, 4))

# ini blocks: the comment lines before ";Name=value", up to the previous blank line.
ini_lines = ini.split('\n')
ini_keys = {}
assert ini_lines.count('RainOpacity=60') == 1, 'combo rain opacity override'
for i, l in enumerate(ini_lines):
    m = re.match(r'^;(\w+)=(\S*)\s*$', l) or re.fullmatch(r'(RainOpacity)=(60)', l)
    if not m: continue
    j = i
    while j > 0 and ini_lines[j - 1].strip(): j -= 1
    ini_keys[m.group(1)] = (m.group(2), '\n'.join(ini_lines[j:i]))

for name, (member, low, high, preset) in keys.items():
    if name in DEVELOPER_KEYS:  # only the combo preset may enable the internal spatial fog switch
        if name == 'SpatialAlphaFog':
            if ini.splitlines().count('SpatialAlphaFog=1') != 1: fail(INI_NAME, name, 'combo must enable spatial fog')
        elif name in ini: fail(INI_NAME, name, 'developer key appears in the ini template')
        if name in readme: fail(README_NAME, name, 'developer key appears in README.txt')
        continue
    if preset[0] != defaults.get(member): fail('quality_settings.h', name, f'Quality preset {preset[0]} != Settings::{member} initialiser {defaults.get(member)}')
    # the ini template documents the key with a commented ";Name=<Quality value>" line
    if name not in ini_keys: fail(INI_NAME, name, 'no commented ";Name=default" line'); 
    else:
        value, block = ini_keys[name]
        expected_value = 60 if name == 'RainOpacity' else preset[0]
        if value != str(expected_value): fail(INI_NAME, name, f'{name}={value}, expected {expected_value}')
        # an "Allowed lo..hi" line in the key's comment block matches the Keys range
        a = re.findall(r'Allowed (\d+)\.\.(\d+)', block)
        if a and (int(a[-1][0]), int(a[-1][1])) != (low, high): fail(INI_NAME, name, f'Allowed {a[-1][0]}..{a[-1][1]} but Keys range is {low}..{high}')
        if not a and (low, high) != (0, 1): fail(INI_NAME, name, f'no "Allowed {low}..{high}" line in its comment block (only 0..1 keys may omit it)')
        # a "q / b / p" presets line is required in the comment block and matches the Keys presets
        p = re.findall(r'(\d+) ?/ ?(\d+) ?/ ?(\d+)', block)
        if not p: fail(INI_NAME, name, 'no "a / b / c" presets line in its comment block')
        elif tuple(int(x) for x in p[-1]) != preset: fail(INI_NAME, name, f'presets {" / ".join(p[-1])} but Keys has {" / ".join(map(str, preset))}')
    # README.txt has the key's row with the Keys presets
    if name not in rows: fail(README_NAME, name, 'no settings table row')
    elif rows[name] != preset: fail(README_NAME, name, f'row {rows[name]} but Keys presets {preset}')

# removed keys left in the docs (FogTemporal and any other deleted key): every documented name must be a real key
for name in ini_keys:
    if name not in keys: fail(INI_NAME, name, 'documented but not a key in quality_settings.h')
for name in rows:
    if name not in keys: fail(README_NAME, name, 'table row but not a key in quality_settings.h')
assert 'FogTemporal' not in keys and 'FogTemporal' not in ini and 'FogTemporal' not in readme  # always on since the game tests; the key is gone

# the origin array holds one slot per key (the header also static_asserts it)
m = re.search(r"char origin\[(\d+)\]=\{([^}]*)\}", code)
if not m or int(m.group(1)) != len(keys) or m.group(2).count("'d'") != len(keys): fail('quality_settings.h', 'origin', f'origin array is not {len(keys)} slots of \'d\'')

if failures:
    print('\n'.join('FAIL ' + f for f in failures)); sys.exit(1)
print(f'PASS quality docs: {len(keys)} keys, {len(rows)} README.txt rows, {len(ini_keys)} ini keys; internal settings and combo overrides checked')
