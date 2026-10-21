#!/usr/bin/env python3
"""Run an existing AppImage with isolated data for a presentation-mode A/B test."""
import argparse
import csv
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('baseline', 'immediate'))
    parser.add_argument('--appimage', required=True, type=Path)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--experiment', action='store_true',
                        help='Queue-skip patch plus alternating direct/parallel Vulkan recording')
    parser.add_argument('--readback-experiment', action='store_true',
                        help='Alternate precise readback prefetch off/on every ten seconds')
    parser.add_argument('--prefetch', choices=('off', 'on'),
                        help='Fixed prefetch setting for separate-process comparisons')
    parser.add_argument('--low-resolution', action='store_true',
                        help='Diagnostic 1280x720 rendering with the engine resolution-scale option')
    args = parser.parse_args()
    if args.experiment and args.readback_experiment:
        parser.error('Choose only one alternating experiment')
    if args.prefetch and (args.experiment or args.readback_experiment or args.low_resolution):
        parser.error('--prefetch requires a fixed 1080p comparison without other experiments')
    readback_test = args.readback_experiment or args.prefetch is not None
    ab_test = args.experiment or args.readback_experiment
    image = args.appimage.expanduser().resolve(strict=True)
    source = Path(os.environ.get('DCPC_USER_DIR') or
                  str(Path(os.environ.get('XDG_DATA_HOME') or str(Path.home() / '.local/share')) /
                      'driveclub-pc')).expanduser().resolve(strict=True)
    if not (source / 'driveclub_pc.ini').is_file():
        parser.error(f'Launcher settings missing: {source}')
    root = Path.home() / 'Downloads' / 'Driveclub-FPS-Tests'
    root.mkdir(parents=True, exist_ok=True)
    suffix = (f'-prefetch-{args.prefetch}-' if args.prefetch else '-readback-ab-' if args.readback_experiment else '-experiment-'
              if args.experiment else '-720p-' if args.low_resolution else '-')
    run = Path(tempfile.mkdtemp(prefix=args.mode + suffix, dir=root))
    data = run / 'data'
    data.mkdir()
    # Private copies preserve the same saved race/settings for both tests. Firmware is read-only
    # input to the emulator and can be shared without duplicating the console dump.
    excluded = {'sys_modules', 'log', 'screenshots', 'captures', 'temp'}
    for entry in source.iterdir():
        if entry.name in excluded:
            continue
        dest = data / entry.name
        if entry.is_dir():
            shutil.copytree(entry, dest)
        elif entry.is_file():
            shutil.copy2(entry, dest)
    if (source / 'sys_modules').is_dir():
        (data / 'sys_modules').symlink_to(source / 'sys_modules', target_is_directory=True)
    settings = data / 'driveclub_pc.ini'
    values = dict(line.split('=', 1) for line in settings.read_text().splitlines()
                  if '=' in line and not line.startswith('#'))
    values.update(target_fps='60', classic_vblank='0', show_fps='1', show_launcher='1')
    if ab_test or args.prefetch:
        values['render_res'] = '1080'
    if readback_test:
        values['readbacks'] = '2'
    if args.low_resolution:
        values['render_res'] = '1080'  # Explicit environment below supplies diagnostic scaling.
    settings.write_text('# Isolated FPS diagnostic settings\n' +
                        ''.join(f'{key}={value}\n' for key, value in values.items()))
    if args.mode == 'immediate' or readback_test:
        # Per-game values override the global config, so update both in the private copy.
        configs = [data / 'config.json', data / 'custom_configs/CUSA00093.json']
        for path in configs:
            config = json.loads(path.read_text()) if path.exists() else {}
            gpu = config.setdefault('GPU', {})
            if args.mode == 'immediate':
                gpu['present_mode'] = 'Immediate'
            if readback_test:
                gpu['readback_offload'] = True
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(config, indent=2) + '\n')
    (run / 'TEST-INFO.txt').write_text(
        f'Mode: {args.mode}, low resolution: {args.low_resolution}, '
        f'experiment: {args.experiment}, readback experiment: {args.readback_experiment}, '
        f'fixed prefetch: {args.prefetch}\nAppImage: {image}\nOriginal data: {source}\n'
        'Target: 60 FPS, classic pacing off. Original settings and saves are preserved.\n'
        'Run the same race for at least 60 seconds, then close the game normally.\n')
    print(f'Testdaten und Ergebnisse: {run}', flush=True)
    if args.prepare_only:
        print('Vorbereitung abgeschlossen; kein Spiel gestartet.')
        return
    env = os.environ.copy()
    env.update(DCPC_USER_DIR=str(data), DCPC_PROFILE='1', DCPC_TARGET_FPS='60',
               DCPC_CLASSIC_VBLANK='0', APPIMAGE_EXTRACT_AND_RUN='1')
    if args.experiment:
        env.update(DCPC_CP_EMPTY_SKIP='1', DCPC_DEFERRED_RECORDING='2',
                   DCPC_VK_STATS='1', DCPC_GUEST_STATS='1')
    if readback_test:
        env.update(DCPC_CP_EMPTY_SKIP='0', DCPC_DEFERRED_RECORDING='0',
                   DCPC_READBACK_PREFETCH='0' if args.prefetch == 'off' else '1',
                   DCPC_READBACK_AB='1' if args.readback_experiment else '0',
                   DCPC_READBACK_REDIRECT='0', DCPC_CP_SCHED_AB='0',
                   DCPC_PERF='0', DCPC_WRITE_WINDOW_AB='0',
                   DCPC_READBACK_HYBRID='0', DCPC_READBACK_HYBRID_AB='0')
    if args.low_resolution:
        env.update(DCPC_RENDER_RES='1280x720', DCPC_GAME_ARGS='-resolutionscale 0.666667',
                   DCPC_WIDESCREEN='', DCPC_VRAM_EXTRA_MB='0')
    seconds = 120 if readback_test else 90 if args.experiment else 60
    print(f'Bitte dieselbe Rennsituation mindestens {seconds} Sekunden testen '
          'und danach das Spiel schließen.', flush=True)
    with (run / 'console.log').open('w') as output:
        result = subprocess.run([str(image), '--launcher'], env=env,
                                stdout=output, stderr=subprocess.STDOUT)
    profile = data / 'log/frame_profile.csv'
    summary = [f'Mode: {args.mode}', f'Fixed prefetch: {args.prefetch}', f'Exit status: {result.returncode}', f'Profile: {profile}']
    if profile.exists():
        with profile.open() as stream:
            rows = list(csv.DictReader(stream))
        rows = [row for row in rows if row.get('frame_ms') and row.get('qpc_ms')]
        if rows:
            end = float(rows[-1]['qpc_ms'])
            rows = [row for row in rows if float(row['qpc_ms']) >= end - 60000]
            summary.append('Last 60 seconds; includes any menus/loading in that interval.')
            for key in ('frame_ms', 'cp_busy_ms', 'cp_idle_ms', 'sched_wait_ms',
                        'present_wait_ms', 'gpu_ms', 'present_hold_max_ms', 'download_cp_ms'):
                samples = [float(row[key]) for row in rows if row.get(key)]
                if samples:
                    summary.append(f'{key}: mean={statistics.mean(samples):.3f}, '
                                   f'median={statistics.median(samples):.3f} ms')
            frame = [float(row['frame_ms']) for row in rows if float(row['frame_ms']) > 0]
            if frame:
                summary.append(f'Average FPS: {1000 / statistics.mean(frame):.2f}')
            if ab_test:
                phases = {0: [], 1: []}
                previous = None
                start = 0.0
                for row in rows:
                    phase = int(row.get('phase') or 0)
                    now = float(row['qpc_ms'])
                    if phase != previous:
                        previous, start = phase, now
                    # Drop the first second after each switch so queued work can drain.
                    if now - start >= 1000 and phase in phases:
                        phases[phase].append(row)
                for phase, samples in phases.items():
                    frames = [float(row['frame_ms']) for row in samples
                              if float(row['frame_ms']) > 0]
                    if frames:
                        if args.readback_experiment:
                            label = 'prefetch on' if phase else 'prefetch off'
                        else:
                            label = 'parallel recording' if phase else 'direct recording'
                        summary.append(f'Phase {phase} ({label}): '
                                       f'{1000 / statistics.mean(frames):.2f} FPS, '
                                       f'{len(frames)} frames')
                        for key in ('sched_wait_ms', 'download_cp_ms', 'readback_record_cp_ms',
                                    'cp_busy_ms', 'cp_idle_ms'):
                            values = [float(row[key]) for row in samples if row.get(key)]
                            if values:
                                summary.append(f'  {key}: {statistics.mean(values):.3f} ms')


    else:
        summary.append('No profile produced. See console.log for startup errors.')
    report = '\n'.join(summary) + '\n'
    (run / 'SUMMARY.txt').write_text(report)
    print(report)


if __name__ == '__main__':
    main()
