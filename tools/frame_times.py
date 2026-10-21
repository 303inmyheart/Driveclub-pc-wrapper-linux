#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Analyze profiler frame durations; percentiles use the nearest-rank definition."""
import argparse
import csv
import math
from pathlib import Path


def valid_rows(rows):
    result = []
    for row in rows:
        try:
            duration, stamp = float(row['frame_ms']), float(row['qpc_ms'])
        except (ValueError, TypeError, KeyError):
            continue
        if math.isfinite(duration) and math.isfinite(stamp) and duration > 0:
            result.append(row)
    return result


def metrics(rows):
    frames = sorted(float(row['frame_ms']) for row in valid_rows(rows))
    if not frames:
        return {}
    def percentile(p):
        return frames[max(0, math.ceil(len(frames) * p) - 1)]
    return dict(frames=len(frames), fps=1000 * len(frames) / sum(frames),
                p50_ms=percentile(.50), p95_ms=percentile(.95), p99_ms=percentile(.99),
                p999_ms=percentile(.999), max_ms=frames[-1],
                over25=sum(x > 25 for x in frames),
                over33=sum(x > 1000 / 30 for x in frames),
                over50=sum(x > 50 for x in frames))


def format_metrics(values):
    if not values:
        return ['No valid frame durations.']
    return [f"Frame samples: {values['frames']}; average FPS: {values['fps']:.2f}",
            'Frame time percentiles (nearest rank): ' + ', '.join(
                f'{label}={values[key]:.3f} ms' for label, key in
                [('p50', 'p50_ms'), ('p95', 'p95_ms'), ('p99', 'p99_ms'), ('p99.9', 'p999_ms')]),
            f"Maximum: {values['max_ms']:.3f} ms; >25 ms: {values['over25']}; "
            f">33.333 ms: {values['over33']}; >50 ms: {values['over50']}"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profiles', type=Path, nargs='+')
    parser.add_argument('--seconds', type=float, default=60)
    args = parser.parse_args()
    for path in args.profiles:
        with path.open() as stream:
            rows = valid_rows(csv.DictReader(stream))
        if rows:
            end = float(rows[-1]['qpc_ms'])
            rows = [r for r in rows if float(r['qpc_ms']) >= end - args.seconds * 1000]
        print(path)
        print('\n'.join(format_metrics(metrics(rows))))
        print('Window can include menus/loading. Inclusive timing columns overlap; '
              'they are not hardware utilisation measurements.\n')


if __name__ == '__main__':
    main()
