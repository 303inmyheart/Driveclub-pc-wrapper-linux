#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Headless launch validation fixtures; every accepted fixture stops at missing firmware."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def sfo(folder, title, category='gd', version='01.28'):
    (folder / 'sce_sys').mkdir(parents=True)
    values = dict(TITLE_ID=title, CATEGORY=category, APP_VER=version)
    keys, data, entries = b'', b'', b''
    for key, value in values.items():
        text = value.encode() + b'\0'
        entries += struct.pack('<HHIII', len(keys), 0x204, len(text), len(text), len(data))
        keys += key.encode() + b'\0'
        data += text
    offset = 20 + len(entries)
    (folder / 'sce_sys/param.sfo').write_bytes(
        b'\0PSF' + struct.pack('<4I', 0x101, offset, offset + len(keys), len(values)) +
        entries + keys + data)
    (folder / 'eboot.bin').write_bytes(b'Test fixture, not a game executable')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix='driveclub-validation-') as temp:
        root = Path(temp)
        sfo(root / 'reference/CUSA00093', 'CUSA00093')
        sfo(root / 'region/CUSA12345', 'CUSA12345')
        sfo(root / 'old/CUSA00093', 'CUSA00093', version='01.27')
        sfo(root / 'mismatch/CUSA12345', 'CUSA12345')
        sfo(root / 'mismatch/CUSA12345-patch', 'CUSA00093', category='gp')
        sfo(root / 'matching/CUSA12345', 'CUSA12345', version='01.00')
        sfo(root / 'matching/CUSA12345-patch', 'CUSA12345', category='gp')
        sfo(root / 'invalid/bad', 'CUSA12A45')
        cases = [('reference', 'firmware modules', False),
                 ('region', 'firmware modules', True), ('old', 'firmware modules', True),
                 ('matching', 'firmware modules', True),
                 ('mismatch', 'title ID does not match', False),
                 ('invalid', 'No PS4 base game', False)]
        for name, expected, experimental in cases:
            env = os.environ.copy()
            env.update(DCPC_USER_DIR=str(root / ('data-' + name)), SDL_VIDEODRIVER='dummy',
                       SDL_VIDEO_DRIVER='dummy', APPIMAGE_EXTRACT_AND_RUN='1')
            result = subprocess.run([str(binary), '--game', str(root / name)], env=env,
                                    text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    timeout=30)
            assert result.returncode == 1, (name, result.returncode, result.stdout[-1500:])
            assert expected in result.stdout, (name, result.stdout[-1500:])
            if experimental:
                assert 'EXPERIMENTAL' in result.stdout, (name, result.stdout[-1500:])
            print(f'PASS: {name}')


if __name__ == '__main__':
    main()
