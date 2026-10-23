#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify the linked SDK's handshake and SET_ACTIVITY against a fake Flatpak client."""
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import tempfile
root = Path(__file__).resolve().parent.parent
library = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='dcpc-rpc-') as tmp:
    tmp = Path(tmp)
    binary = tmp / 'client'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-pthread',
                    '-I' + str(root / 'externals/discord-rpc/include'),
                    str(root / 'tests/discord_rpc_protocol_smoke.cpp'), str(library),
                    '-o', str(binary)], check=True)
    path = tmp / 'app/com.discordapp.Discord/discord-ipc-0'
    path.parent.mkdir(parents=True)
    def receive(conn):
        def exact(count):
            out = b''
            while len(out) < count:
                part = conn.recv(count - len(out))
                assert part, 'Unexpected end of RPC stream'
                out += part
            return out
        opcode, size = struct.unpack('<II', exact(8))
        assert size < 65536
        return opcode, json.loads(exact(size))
    with socket.socket(socket.AF_UNIX) as server:
        server.bind(str(path)); server.listen(1); server.settimeout(5)
        child = subprocess.Popen([str(binary)], env=dict(os.environ, XDG_RUNTIME_DIR=str(tmp)))
        try:
            conn, _ = server.accept()
            with conn:
                conn.settimeout(5)
                op, hello = receive(conn)
                assert op == 0 and hello['client_id'] == '1139939140494971051'
                payload = json.dumps({'cmd': 'DISPATCH', 'evt': 'READY', 'data': {
                    'user': {'id': '0', 'username': 'local-fixture', 'discriminator': '0000'}}}).encode()
                conn.sendall(struct.pack('<II', 1, len(payload)) + payload)
                for _ in range(10):
                    op, message = receive(conn)
                    if message.get('cmd') == 'SET_ACTIVITY':
                        assert message['args']['activity']['state'] == 'Driveclub IPC test'
                        break
                else:
                    raise AssertionError('Missing presence update')
                assert child.wait(timeout=5) == 0
        finally:
            if child.poll() is None: child.kill(); child.wait()
    print('PASS: Flatpak IPC handshake, READY callback and SET_ACTIVITY payload (fake client only)')
