"""Summarize explicitly marked, stationary log windows without running the game."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics


def summarize(text, windows):
    intervals, profiles, frame_profiles, cpu_profiles, hdr_cpu_profiles, work = [], [], [], [], [], []
    for line in text.splitlines():
        if line.startswith('FRAME interval '):
            row = dict(re.findall(r'(\w+)=([^ ]+)', line))
            if row.get('samples') == '120' and row.get('focus') == '1' and row.get('enabled') == '1':
                intervals.append(row)
        elif line.startswith(('GPU profile ', 'GPU frame ', 'CPU profile ', 'CPU HDR ')):
            frame = re.search(r'frame=(\d+)', line)
            if frame:
                rows = hdr_cpu_profiles if line.startswith('CPU HDR ') else frame_profiles if line.startswith('GPU frame ') else cpu_profiles if line.startswith('CPU profile ') else profiles
                rows.append((int(frame[1]), dict(re.findall(r'(\w+)=([\d.]+)ms', line))))
        elif line.startswith('HDR_WORK '):
            work.append(dict(re.findall(r'(\w+)=(\d+)', line)))
    report = {'scope': 'User-marked stationary windows; excludes partial/unfocused/disabled intervals. Live weather and GPU clocks are uncontrolled. Stage medians are independent and must not be summed.', 'windows': {}}
    for label, first, last in windows:
        if first >= last or label in report['windows']:
            raise ValueError('Window labels must be unique, with first < last')
        # An interval belongs only if all 120 constituent frames fit the window.
        rows = [r for r in intervals if first + 119 <= int(r['frame']) <= last]
        if len(rows) < 2:
            raise ValueError(f'{label}: fewer than two complete focused intervals')
        times = [float(r['meanMs']) for r in rows]
        if any(not math.isfinite(t) or t <= 0 for t in times) or len({r.get('device') for r in rows}) != 1:
            raise ValueError(f'{label}: invalid frame times or multiple devices')
        med = statistics.median(times)
        def medians(points):
            stages = {}
            for frame, values in points:
                if first <= frame <= last:
                    for key, value in values.items():
                        stages.setdefault(key, []).append(float(value))
            return {k: statistics.median(v) for k, v in stages.items()}
        counters = [r for r in work if first <= int(r['frame']) <= last]
        delta = {k: int(counters[-1][k]) - int(counters[0][k]) for k in counters[0] if k != 'frame' and k in counters[-1]} if len(counters) > 1 else {}
        if any(v < 0 for v in delta.values()):
            raise ValueError(f'{label}: counters reset inside the window')
        report['windows'][label] = dict(frames=[first, last], intervals=len(rows), median_interval_mean_ms=med,
                                       equivalent_fps=1000/med, stage_medians_ms=medians(profiles),
                                       frame_gpu_stage_medians_ms=medians(frame_profiles), cpu_profile_medians_ms=medians(cpu_profiles),
                                       hdr_cpu_profile_medians_ms=medians(hdr_cpu_profiles),
                                       hdr_work_delta=delta)
    return report


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--window', action='append', required=True, help='LABEL:FIRST_FRAME:LAST_FRAME, excluding menu/capture/warm-up frames')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    windows = [(label, int(first), int(last)) for label, first, last in (w.split(':') for w in args.window)]
    raw = args.log.read_bytes()
    report = summarize(raw.decode('utf-8', errors='replace'), windows)
    report['log_sha256'] = hashlib.sha256(raw).hexdigest()
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(args.output)


if __name__ == '__main__':
    main()
