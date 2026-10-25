#!/usr/bin/env python3
"""
Pulls the BepInEx runtime out of an APK that already ships it, into resources/.

  python3 tools/extract_runtime.py template.apk [--out resources]

  so/         libfusion.so libcoreclr.so libclrjit.so libc++_shared.so ...
  dotnet/     managed runtime
  BepInEx/    core/ plugins/ config/ unity-libs/

libmain.so is not taken from here; patch.py either uses resources/so/libmain.so
or rewrites the one inside the template.

The output is build input, whether you commit it is up to you.
"""
import argparse
import os
import shutil
import sys
import zipfile

SO_PREFIX = 'lib/arm64-v8a/'
# so files we take from the template; libmain.so is not among them because the
# loader we ship patches it itself
SO_WHITELIST = [
    'libfusion.so', 'libcoreclr.so', 'libclrjit.so', 'libc++_shared.so',
    'libcrypto.so', 'libssl.so', 'libdobby.so', 'libmscordaccore.so',
    'libmscordbi.so', 'libSystem.Native.so',
    'libSystem.Globalization.Native.so',
    'libSystem.IO.Compression.Native.so',
    'libSystem.Security.Cryptography.Native.OpenSsl.so',
]

# candidate roots inside the APK for each runtime piece
ASSET_ROOTS = [
    ('assets/BepInEx/', 'BepInEx'),
    ('assets/dotnet/', 'dotnet'),
    ('assets/BepInEx/dotnet/', 'dotnet'),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('apk')
    ap.add_argument('--out', default=os.path.join(os.path.dirname(__file__), '..', 'resources'))
    a = ap.parse_args()

    out = os.path.abspath(a.out)
    so_dir = os.path.join(out, 'so')
    os.makedirs(so_dir, exist_ok=True)

    z = zipfile.ZipFile(a.apk)
    names = z.namelist()

    # 1. native libraries
    so_count = 0
    for n in SO_WHITELIST:
        p = SO_PREFIX + n
        if p in names:
            with open(os.path.join(so_dir, n), 'wb') as f:
                f.write(z.read(p))
            so_count += 1
            print('  so  ', n)
        else:
            print('  miss', n)

    # 2. managed runtime trees
    for root, dest in ASSET_ROOTS:
        entries = [n for n in names if n.startswith(root) and not n.endswith('/')]
        if not entries:
            continue
        dst_root = os.path.join(out, dest)
        written = 0
        for n in entries:
            rel = n[len(root):]
            t = os.path.join(dst_root, rel)
            os.makedirs(os.path.dirname(t), exist_ok=True)
            with open(t, 'wb') as f:
                f.write(z.read(n))
            written += 1
        print('  dir  %s -> %s (%d files)' % (root, dest, written))

    # 3. unity-libs zip comes from BepInEx, note it for the user
    print()
    print('done: %d so files' % so_count)
    print('unity-libs: put BepInEx/unity-libs/<version>.zip in place '
          '(https://unity.bepinex.dev/libraries/)')


if __name__ == '__main__':
    main()