"""Reject contaminated/ambiguous performance windows; no GPU or client needed."""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from analyze_hdr_log import summarize


def interval(frame, ms, extra=''):
    return f'FRAME interval device=1 frame={frame} tick=1 enabled=1 samples=120 meanMs={ms} focus=1 {extra}\n'


text = interval(100, 99) + interval(220, 10) + interval(340, 12)
text += interval(460, 99).replace('focus=1', 'focus=0')
text += interval(580, 99).replace('samples=120', 'samples=10')
text += interval(700, 99).replace('enabled=1', 'enabled=0')
text += 'GPU profile frame=300 total=9ms FogMarch=1.2ms FogClouds=0.6ms\n'
text += 'GPU frame frame=300 total=12ms GameScene=7ms Effects=3ms tail=2ms\n'
text += 'GPU frame frame=340 total=14ms GameScene=9ms Effects=3ms tail=2ms\n'
text += 'GPU frame frame=701 total=999ms tail=999ms\n'
text += 'CPU profile frame=300 drawGate=0.2ms capture=1ms effects=0.7ms cleanup=0.3ms total=2.2ms\n'
text += 'CPU HDR frame=300 admission=0.1ms boundary=0.9ms finish=0.03ms transferWithNativeDraw=0.3ms\n'
text += 'HDR_WORK frame=200 compatibilityLayers=2 multiTextureDirect=4\nHDR_WORK frame=600 compatibilityLayers=2 multiTextureDirect=20\n'
row = summarize(text, [('HDR', 100, 700)])['windows']['HDR']
assert row['intervals'] == 2 and row['median_interval_mean_ms'] == 11
assert row['stage_medians_ms']['FogMarch'] == 1.2
assert row['stage_medians_ms']['total'] == 9
assert row['frame_gpu_stage_medians_ms'] == dict(total=13, GameScene=8, Effects=3, tail=2)
assert row['cpu_profile_medians_ms']['total'] == 2.2
assert row['hdr_cpu_profile_medians_ms'] == dict(admission=.1, boundary=.9, finish=.03, transferWithNativeDraw=.3)
assert not summarize(interval(220, 10) + interval(340, 12), [('SDR', 100, 340)])['windows']['SDR']['frame_gpu_stage_medians_ms']
assert row['hdr_work_delta'] == dict(compatibilityLayers=0, multiTextureDirect=16)
for source, windows in [(text, [('HDR', 1, 1)]), (text, [('HDR', 100, 220)]),
                        (text, [('HDR', 100, 700), ('HDR', 100, 700)]),
                        (text.replace('meanMs=10', 'meanMs=nan'), [('HDR', 100, 700)]),
                        (text.replace('frame=340', 'frame=340 device=2'), [('HDR', 100, 700)]),
                        (text.replace('multiTextureDirect=20', 'multiTextureDirect=0'), [('HDR', 100, 700)])]:
    try:
        summarize(source, windows)
    except ValueError:
        continue
    raise AssertionError('Invalid benchmark window accepted')
print('PASS HDR log summary: whole intervals, focus, partials, disabled effects, stages, counter resets, unique windows')
