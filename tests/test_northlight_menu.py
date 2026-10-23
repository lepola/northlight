# northlight-test: requires=lua51
"""Execute the addon with a Lua 5.1 FrameXML mock, without the WoW process."""
from pathlib import Path
import os
import re
import sys
if os.environ.get('NORTHLIGHT_LUA_PYTHONPATH'):
    sys.path.insert(0, os.environ['NORTHLIGHT_LUA_PYTHONPATH'])
from lupa.lua51 import LuaRuntime

root = Path(__file__).resolve().parents[1]
keys = re.findall(r'\{"(\w+)",&Settings::\w+,(\d+),(\d+),\{([\d,]+)\}', (root/'src/core/quality_settings.h').read_text())
fields=[]
for key,low,high,presets in keys:
    if key in ('GI','Weather','AOMethod'): continue
    kind='check' if low=='0' and high=='1' else 'slider'
    value=presets.split(',')[0]
    fields.append('\t'.join(['F','Q','Quality',key,kind,low,high,'1',value,value,value,value,'F',presets,'f',value,value,'1'])+'\n')
for key,value,low,high in [('HDRPaperWhite','203','80','400'),('HDRUIWhite','160','40','400'),('HDRMaxNits','1000','80','10000'),('HDRContrast','1','0.5','2'),('HDRExposure','0','-4','4')]:
    step = '0.05' if key in ('HDRContrast','HDRExposure') else '1'
    fields.append('\t'.join(['F','R','Renderer',key,'slider',low,high,step,value,value,value,value,'F','','f',value,value,'1'])+'\n')
# Optional renderer capabilities are bridge fixtures in this standalone menu PR.
optional_keys = [
    ('AutoBump','0','1','1,0,0'), ('AutoBumpStrength','0','150','75,60,40'),
    ('AutoBumpDistance','8','96','48,36,24'), ('AutoBumpTerrain','0','2','2,1,1'),
    ('AutoBumpWalls','0','2','2,1,1'), ('AutoBumpGrass','0','2','1,1,1'),
    ('AutoParallaxShadows','0','1','1,0,0'), ('AutoParallaxShadowStrength','0','100','70,60,50'),
    ('AutoParallaxShadowSteps','4','16','8,6,4'), ('AutoCavity','0','1','1,0,0'),
    ('AutoCavityStrength','0','100','35,30,25'),
]
native_keys = {key for key, *_ in keys}
optional_fields = []
for key,low,high,presets in optional_keys:
    if key in native_keys: continue
    kind = 'check' if low == '0' and high == '1' else 'slider'
    value = presets.split(',')[0]
    optional_fields.append('\t'.join(['F','Q','Quality',key,kind,low,high,'1',value,value,value,value,'F',presets,'f',value,value,'1'])+'\n')
for available,width,hidden,scenario in ((False,420,False,'menu'),(True,420,False,'menu'),(True,360,False,'menu'),(True,420,True,'menu'),(True,420,False,'layout'),(True,420,False,'latency'),(True,420,False,'saving'),(True,420,False,'inheritance'),(True,420,False,'ao'),(True,420,False,'loading'),(True,420,False,'bump'),(True,413,False,'presets'),(True,413,False,'inputs')):
    lua = LuaRuntime()
    assert lua.eval('_VERSION') == 'Lua 5.1'
    lua.globals().HDR_AVAILABLE = available
    lua.globals().HIDE_TRANSPORT = hidden
    lua.globals().PANEL_WIDTH = width
    lua.globals().EXTRA_FIELDS = ''.join(fields + (optional_fields if scenario in ('bump','presets','inputs','saving') else []))
    lua.globals().UNSUPPORTED_KEYS = ' '.join(key for key, *_ in optional_keys if key not in native_keys)
    lua.globals().EXPECTED_KEYS = ' '.join(x[0] for x in keys)
    lua.globals().AO_PREVIEW_AVAILABLE = scenario == 'ao'
    lua.execute((root / 'tests/northlight_menu_mock.lua').read_text(encoding='utf-8'))
    for filename in ('NorthlightAPI.lua', 'NorthlightMenu.lua'):
        lua.execute((root / 'renderer/addons/NorthlightSettings' / filename).read_text(encoding='utf-8'))
    if scenario == 'bump': lua.globals().CheckBumpMenu()
    elif scenario == 'presets': lua.globals().CheckPresetConfirmation()
    elif scenario == 'inputs': lua.globals().CheckNumericInputs()
    elif scenario == 'layout': lua.globals().CheckLayoutFailure()
    elif scenario == 'latency': lua.globals().CheckApplyLatency()
    elif scenario == 'saving': lua.globals().CheckApplyEditing()
    elif scenario == 'inheritance': lua.globals().CheckAcknowledgedInheritance()
    elif scenario == 'ao': lua.globals().CheckAOPreview()
    elif scenario == 'loading':
        lua.globals().CheckLoadingRecovery()
        saved_cache = lua.globals().ExportCache()
        reload = LuaRuntime()
        reload.globals().NorthlightSettingsCache = reload.table_from({'version': 9, 'text': saved_cache})
        reload.globals().HDR_AVAILABLE = True
        reload.execute((root / 'tests/northlight_menu_mock.lua').read_text(encoding='utf-8'))
        for filename in ('NorthlightAPI.lua', 'NorthlightMenu.lua'):
            reload.execute((root / 'renderer/addons/NorthlightSettings' / filename).read_text(encoding='utf-8'))
        reload.globals().CheckCachedLoading()
    else: lua.globals().CheckMenu()
