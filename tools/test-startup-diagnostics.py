#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check isolated fatal-signal recording and original signal exit semantics."""
import os
from pathlib import Path
import resource
import signal
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='dcpc-startup-test-') as temp:
    work = Path(temp)
    source = work / 'test.cpp'
    source.write_text('''#include "driveclub/startup_diagnostics.h"
#include <cstring>
int main(int argc, char** argv) {
 DriveclubPC::StartupDiagnostics::Initialize(argv[1]);
 DriveclubPC::StartupDiagnostics::Mark("test: reached startup");
 DriveclubPC::StartupDiagnostics::Module("libFixture.sprx", "HLE callback enter");
 if (argc > 2) raise(atoi(argv[2]));
}
''')
    binary = work / 'test'
    subprocess.run(['g++', '-std=c++20', '-I', str(root / 'src'), str(source), '-o', str(binary)], check=True)
    def no_core():
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    def run(name, sig=None, enabled=True):
        directory = work / name
        directory.mkdir()
        env = dict(os.environ, DCPC_STARTUP_DIAGNOSTICS='1' if enabled else '0')
        proc = subprocess.run([str(binary), str(directory)] + ([str(sig)] if sig else []),
                              env=env, preexec_fn=no_core, capture_output=True)
        files = list(directory.glob('startup-*.log'))
        assert proc.returncode == (-sig if sig else 0), (name, proc.returncode)
        if not enabled:
            assert not files
            return
        assert len(files) == 1
        text = files[0].read_text()
        assert 'test: reached startup' in text and '--- process mappings ---' in text
        assert 'HLE module: libFixture.sprx | HLE callback enter' in text
        assert bool('FATAL signal=' in text) == bool(sig)
        if sig:
            assert '--- process mappings ---' in text.split('FATAL signal=', 1)[1]
            assert f'signal=0x{sig:016x}' in text and ' rip=0x' in text and ' rsp=0x' in text
        assert files[0].stat().st_mode & 0o777 == 0o600
    run('disabled', enabled=False)
    run('normal')
    for sig in (signal.SIGABRT, signal.SIGILL, signal.SIGSEGV, signal.SIGBUS, signal.SIGFPE):
        run(str(sig), sig)
print('PASS: opt-in, normal exit, private logs, mappings, five fatal signals and signal exit status')
