# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only Linux process/GPU samples, aligned to the frame profiler's monotonic clock."""
import csv
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time


class ResourceSampler:
    def __init__(self, pid, output):
        self.pid = pid
        self.output = Path(output)
        self.stop_event = threading.Event()
        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.previous = {}
        self.hz = os.sysconf('SC_CLK_TCK')
        self.nvidia = shutil.which('nvidia-smi')

    def start(self):
        self.thread.start()

    def stop(self):
        self.stop_event.set()
        self.thread.join(timeout=8)

    def process_tree(self):
        pending, found = [self.pid], set()
        while pending:
            pid = pending.pop()
            if pid in found:
                continue
            found.add(pid)
            try:
                children = Path(f'/proc/{pid}/task/{pid}/children').read_text().split()
                pending.extend(map(int, children))
            except (OSError, ValueError):
                pass
        return found

    def cpu_samples(self, now):
        samples, current = [], {}
        for pid in self.process_tree():
            try:
                tasks = list(Path(f'/proc/{pid}/task').iterdir())
            except OSError:
                continue
            for task in tasks:
                try:
                    stat = (task / 'stat').read_text()
                    fields = stat[stat.rindex(')') + 2:].split()
                    ticks = int(fields[11]) + int(fields[12])
                    tid = int(task.name)
                    # Include process start time to avoid confusing a reused thread ID.
                    key = (tid, int(fields[19]))
                    previous = self.previous.get(key)
                    percent = ((ticks - previous[0]) / self.hz / (now - previous[1]) * 100
                               if previous and now > previous[1] else 0)
                    current[key] = (ticks, now)
                    allowed = next((line.split(':', 1)[1].strip()
                                    for line in (task / 'status').read_text().splitlines()
                                    if line.startswith('Cpus_allowed_list:')), '')
                    samples.append(dict(pid=pid, tid=tid, name=(task / 'comm').read_text().strip(),
                                        cpu=int(fields[36]), cpu_percent=round(percent, 2),
                                        allowed=allowed))
                except (OSError, ValueError, IndexError):
                    continue
        self.previous = current
        samples.sort(key=lambda row: row['cpu_percent'], reverse=True)
        selected = samples[:8]
        for row in samples[8:]:
            if row['name'].startswith(('Game:Main', 'shadPS4:GpuComm', 'dcpc:QueueSubmi',
                                       'dcpc:VkRecorder')):
                selected.append(row)
        return selected

    def gpu_sample(self):
        if not self.nvidia:
            return {'error': 'nvidia-smi unavailable'}
        fields = ('name,pstate,temperature.gpu,utilization.gpu,utilization.memory,'
                  'memory.used,memory.total,power.draw,power.limit,clocks.gr,clocks.mem')
        try:
            result = subprocess.run([self.nvidia, '--query-gpu=' + fields, '--format=csv,nounits'],
                                    capture_output=True, text=True, timeout=5)
            if result.returncode:
                return {'error': result.stderr.strip() or result.stdout.strip()}
            return list(csv.DictReader(io.StringIO(result.stdout), skipinitialspace=True))
        except (OSError, subprocess.TimeoutExpired) as error:
            return {'error': str(error)}

    def loop(self):
        with self.output.open('w') as output:
            while not self.stop_event.is_set():
                now = time.monotonic()
                record = dict(monotonic_ms=now * 1000, threads=self.cpu_samples(now))
                record['gpu'] = self.gpu_sample()
                frequencies = {}
                for path in Path('/sys/devices/system/cpu').glob('cpu[0-9]*/cpufreq/scaling_cur_freq'):
                    try:
                        frequencies[path.parent.parent.name] = int(path.read_text())
                    except (OSError, ValueError):
                        pass
                record['cpu_frequency_khz'] = frequencies
                temperatures = {}
                for hwmon in Path('/sys/class/hwmon').glob('hwmon*'):
                    try:
                        name = (hwmon / 'name').read_text().strip()
                        if name in ('coretemp', 'k10temp', 'zenpower'):
                            temperatures[hwmon.name + ':' + name] = int((hwmon / 'temp1_input').read_text()) / 1000
                    except (OSError, ValueError):
                        pass
                record['cpu_temperature_c'] = temperatures
                output.write(json.dumps(record) + '\n')
                output.flush()
                self.stop_event.wait(2)
