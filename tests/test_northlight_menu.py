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
    if key in ('GI','Weather','AOMethod','AOQuality'): continue
    kind='check' if low=='0' and high=='1' else 'slider'
    value=presets.split(',')[0]
    fields.append('\t'.join(['F','Q','Quality',key,kind,low,high,'1',value,value,value,value,'F',presets,'f',value,value,'1'])+'\n')
for key,value,low,high in [('HDRPaperWhite','203','80','400'),('HDRUIWhite','160','40','400'),('HDRMaxNits','1000','80','10000'),('HDRContrast','1','0.5','2'),('HDRExposure','0','-4','4')]:
    step = '0.05' if key in ('HDRContrast','HDRExposure') else '1'
    fields.append('\t'.join(['F','R','Renderer',key,'slider',low,high,step,value,value,value,value,'F','','f',value,value,'1'])+'\n')
for available,width,hidden,scenario in ((False,420,False,'menu'),(True,420,False,'menu'),(True,360,False,'menu'),(True,420,True,'menu'),(True,420,False,'layout'),(True,420,False,'latency'),(True,420,False,'saving'),(True,420,False,'inheritance'),(True,420,False,'ao'),(True,420,False,'aoquality'),(True,420,False,'loading'),(True,420,False,'bump'),(True,413,False,'presets'),(True,413,False,'inputs')):
    lua = LuaRuntime()
    assert lua.eval('_VERSION') == 'Lua 5.1'
    lua.globals().HDR_AVAILABLE = available
    lua.globals().HIDE_TRANSPORT = hidden
    lua.globals().PANEL_WIDTH = width
    scenario_fields = fields
    if scenario == 'bump':
        scenario_fields=[]
        for field in fields:
            parts=field.rstrip('\n').split('\t')
            if parts[3] in ('AutoBumpTerrain','AutoBumpWalls'):
                for index in (8,9,11,15): parts[index]='2'
            scenario_fields.append('\t'.join(parts)+'\n')
    lua.globals().EXTRA_FIELDS = ''.join(scenario_fields)
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
    elif scenario == 'aoquality': lua.globals().CheckAOQuality()
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
