#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the Linux RPC transport against local fake sockets; never contacts Discord."""
import os
from pathlib import Path
import socket
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix='dcpc-ipc-') as tmp:
    tmp = Path(tmp)
    binary = tmp / 'client'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-pthread', '-Wall', '-Wextra',
                    '-Werror', '-I' + str(root / 'externals/discord-rpc/src'),
                    str(root / 'tests/discord_ipc_smoke.cpp'),
                    str(root / 'src/common/discord_ipc_linux.cpp'), '-o', str(binary)], check=True)
    env = dict(os.environ, XDG_RUNTIME_DIR=str(tmp))
    for relative in ('discord-ipc-0', 'app/com.discordapp.Discord/discord-ipc-3',
                     'app/dev.vencord.Vesktop/discord-ipc-7'):
        path = tmp / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        with socket.socket(socket.AF_UNIX) as server:
            server.bind(str(path)); server.listen(1); server.settimeout(5)
            child = subprocess.Popen([str(binary)], env=env)
            try:
                conn, _ = server.accept()
                with conn:
                    assert conn.recv(4) == b'PING'
                    conn.sendall(b'PONG')
                assert child.wait(timeout=5) == 0
            finally:
                if child.poll() is None: child.kill(); child.wait()
                path.unlink()
        print('PASS:', relative)
    subprocess.run([str(binary), 'missing'], env=env, check=True, timeout=10)
    print('PASS: missing client and repeated reconnect attempts do not leak descriptors')
