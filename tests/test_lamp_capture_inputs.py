#!/usr/bin/env python3
# northlight-test:
"""Capture replay rejects missing promised masks and conflicting HDR transfers."""
import copy
from pathlib import Path
import struct
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
from render_lamp_self_shadow import replay_contract


def image(path, fmt=113):
    stride = 8 if fmt == 113 else 4
    path.write_bytes(struct.pack('<5I', 0x31524746, 1, 1, fmt, stride)+bytes(stride))


with tempfile.TemporaryDirectory(dir=fp.output_dir()) as folder:
    prefix = str(Path(folder)/'capture-1-')
    base = Path(prefix+'hdr-base.fgr'); image(base)
    water = Path(prefix+'lighting-input-g0-s11.fgr'); image(water, 114)
    records = [dict(event='begin'),
               dict(event='input-binding', group=0, slot=11, null=False, snapshot_group=0,
                    file_base='lighting-input-g0-s11'),
               dict(event='end', render_completed=True, export_complete=True)]
    plan = {'material_group': 3, 'hdr': True}
    contract = replay_contract(records, prefix, plan)
    assert not contract['hdr_base_srgb'] and contract['masks'] == {
        'effect': 'historical-omission', 'rain': 'historical-omission', 'water': 'captured'}

    def rejected(rows):
        try:
            replay_contract(rows, prefix, plan)
        except (ValueError, AssertionError, struct.error):
            return
        raise AssertionError('Corrupt or ambiguous capture was accepted')

    for key in ('render_completed', 'export_complete'):
        rows = copy.deepcopy(records); rows[-1][key] = False; rejected(rows)
    rejected(records[:-1])
    rows = copy.deepcopy(records); rows.insert(1, dict(event='unavailable')); rejected(rows)
    rows = copy.deepcopy(records); rows.insert(1, dict(event='file', name='depth.fgr', saved=False)); rejected(rows)
    rows = copy.deepcopy(records); rows.insert(1, dict(event='file', name='lighting-input-g3-s7.fgr', saved=True)); rejected(rows)
    rows = copy.deepcopy(records); rows.insert(1, dict(event='input-binding', group=2, slot=7,
               null=False, snapshot_group=2, file_base='lighting-input-g2-s7')); rejected(rows)
    for slot in (7, 13):
        path = Path(prefix+f'lighting-input-g3-s{slot}.fgr')
        binding = dict(event='input-binding', group=3, slot=slot, null=False, snapshot_group=3,
                       file_base=f'lighting-input-g3-s{slot}')
        rows = copy.deepcopy(records); rows.insert(1, binding)
        rejected(rows)  # A modern promise cannot fall back to a neutral texture.
        path.write_bytes(b'corrupt'); rejected(rows)
        image(path); assert 'captured' in replay_contract(rows, prefix, plan)['masks'].values()
        rejected(records)  # Unjournaled files could be stale capture output.
        rows[1] = {**binding, 'null': True, 'file_base': ''}; rejected(rows)
        path.unlink(); assert 'null' in replay_contract(rows, prefix, plan)['masks'].values()
        rows[1] = binding; image(path); rows.insert(2, {**binding, 'file_base': 'different'}); rejected(rows)
        path.unlink()
    water.unlink(); rejected(records); image(water, 114)
    for fmt in (21, 22, 113):
        image(base, fmt); expected = fmt != 113
        assert replay_contract(records, prefix, plan)['hdr_base_srgb'] == expected
        rows = copy.deepcopy(records); rows.insert(1, dict(event='pass', index=1, entry='WorldCompositeHdr'))
        rows.insert(2, {'event': 'sampler-state', 'pass': 1, 'slot': 2, 'state': 11, 'value': int(expected)})
        assert replay_contract(rows, prefix, plan)['hdr_base_srgb'] == expected
        rows[2]['value'] = int(not expected); rejected(rows)
    image(base, 114); rejected(records)
    base.unlink()
    assert not replay_contract(records, prefix, {**plan, 'hdr': False})['hdr_base_srgb']

print('LAMP_CAPTURE_INPUTS_PASS historical_omissions modern_promises null_masks incomplete_corrupt encoded_UNORM linear_FP16 recorded_transfer_conflicts')
