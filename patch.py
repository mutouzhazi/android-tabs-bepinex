#!/usr/bin/env python3
"""
BepInEx.Android APK patcher for TABS.

Swaps libmain.so for one that starts fusion, adds the .NET runtime .so files,
embeds assets/BepInEx and assets/dotnet, and re-aligns resources.arsc.

  python3 patch.py --src TABS.apk --out TABS_patched.apk --template RW.apk

--template is any APK that already runs BepInEx 6, only consulted when
resources/so/libmain.so is missing.

resources/
  so/         libmain.so libfusion.so libcoreclr.so libclrjit.so libc++_shared.so ...
  dotnet/     managed runtime
  BepInEx/    core/ plugins/ config/ unity-libs/
"""
import argparse
import os
import shutil
import struct
import sys
import zipfile

LIBDIR = 'lib/arm64-v8a/'
TEMPLATE_UNITY = b'2020.3.38'

# must be stored uncompressed inside the APK
SO_NAMES = [
    'libfusion.so', 'libcoreclr.so', 'libclrjit.so', 'libc++_shared.so',
    'libcrypto.so', 'libssl.so', 'libdobby.so', 'libmscordaccore.so',
    'libmscordbi.so', 'libSystem.Native.so',
    'libSystem.Globalization.Native.so',
    'libSystem.IO.Compression.Native.so',
    'libSystem.Security.Cryptography.Native.OpenSsl.so',
]

# (dir under resources, prefix inside the APK)
ASSET_TREES = [
    ('BepInEx', 'assets/BepInEx/'),
    ('dotnet', 'assets/dotnet/'),
]


def align_arsc(zout, info, data):
    """resources.arsc must be stored with a 4-byte aligned payload."""
    ni = zipfile.ZipInfo(info.filename, info.date_time)
    ni.external_attr = info.external_attr
    ni.compress_type = zipfile.ZIP_STORED
    name_len = len(info.filename.encode())
    t = (zout.fp.tell() + 30 + name_len) % 4
    ni.extra = (struct.pack('<HH', 0xD935, 4 - t) + b'\x00' * (4 - t)) if t else b''
    zout.writestr(ni, data)


def rewrite_unity_version(raw, from_ver, to_ver):
    if len(to_ver) > len(from_ver):
        sys.exit('target version too long: %r (max %d bytes)' % (to_ver, len(from_ver)))
    if raw.find(from_ver) < 0:
        print('  [warn] %r not found in libmain.so, leaving it alone' % from_ver)
        return raw
    return raw.replace(from_ver, to_ver + b'\x00' * (len(from_ver) - len(to_ver)))


def add_stored(zout, name, data):
    ni = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
    ni.compress_type = zipfile.ZIP_STORED
    ni.external_attr = 0o755 << 16
    zout.writestr(ni, data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--src', required=True, help='original game APK')
    ap.add_argument('--out', required=True, help='output APK')
    ap.add_argument('--resources', default='resources')
    ap.add_argument('--template', default=None,
                    help='APK with BepInEx 6, only needed if resources/so/libmain.so is missing')
    ap.add_argument('--unity-version', default='2022.3.62')
    ap.add_argument('--fusion', default=None,
                    help='libfusion.so patched with DT_NEEDED (overrides resources/so/)')
    ap.add_argument('--loader', default='build/libtabsopt.so',
                    help='compiled libtabsopt.so (native/build.sh)')
    a = ap.parse_args()

    res = os.path.abspath(a.resources)
    so_dir = os.path.join(res, 'so')

    # ---- libmain ----
    libmain_path = os.path.join(so_dir, 'libmain.so')
    if os.path.isfile(libmain_path):
        libmain = open(libmain_path, 'rb').read()
        print('libmain.so: from resources/so (version untouched)')
    elif a.template:
        tpl = zipfile.ZipFile(a.template)
        p = LIBDIR + 'libmain.so'
        if p not in tpl.namelist():
            sys.exit('template has no ' + p)
        libmain = rewrite_unity_version(tpl.read(p), TEMPLATE_UNITY, a.unity_version.encode())
        tpl.close()
        print('libmain.so: from template, unityVersion -> %s' % a.unity_version)
    else:
        sys.exit('need resources/so/libmain.so or --template')

    # ---- loader (libtabsopt.so) ----
    print('loader:', a.loader if os.path.isfile(a.loader) else '(missing)')
    if not os.path.isfile(a.loader):
        sys.exit('missing loader: %s  (build it with native/build.sh)' % a.loader)
    loader = open(a.loader, 'rb').read()

    # ---- runtime so files ----
    runtime = {}
    for n in SO_NAMES:
        p = os.path.join(so_dir, n)
        if os.path.isfile(p):
            runtime[n] = open(p, 'rb').read()
        else:
            print('  [warn] missing ' + p)
    if a.fusion:
        runtime['libfusion.so'] = open(a.fusion, 'rb').read()

    # ---- assets trees ----
    assets = []
    for sub, prefix in ASSET_TREES:
        root = os.path.join(res, sub)
        if not os.path.isdir(root):
            continue
        for dirpath, _, files in os.walk(root):
            for f in files:
                full = os.path.join(dirpath, f)
                rel = os.path.relpath(full, root).replace(os.sep, '/')
                assets.append((prefix + rel, full))
    print('assets: %d files' % len(assets))

    src = zipfile.ZipFile(a.src)
    out = zipfile.ZipFile(a.out, 'w', allowZip64=True)
    skip = set(LIBDIR + k for k in runtime)
    skip.add(LIBDIR + 'libtabsopt.so')
    skip |= set(p for p, _ in assets)

    for info in src.infolist():
        fn = info.filename
        if fn == 'resources.arsc':
            align_arsc(out, info, src.read(info))
            continue
        if fn == LIBDIR + 'libmain.so':
            add_stored(out, fn, libmain)
            print('  ~ libmain.so')
            continue
        if fn in skip:
            continue  # replaced below
        ni = zipfile.ZipInfo(fn, info.date_time)
        ni.external_attr = info.external_attr
        ni.compress_type = info.compress_type
        with src.open(info) as s, out.open(ni, 'w') as t:
            shutil.copyfileobj(s, t, 4 * 1024 * 1024)
        if info.file_size > 200 * 1024 * 1024:
            print('  ... %s' % fn)

    for n, d in runtime.items():
        add_stored(out, LIBDIR + n, d)
        print('  + %s (%d bytes)' % (n, len(d)))

    add_stored(out, LIBDIR + 'libtabsopt.so', loader)
    print('  + libtabsopt.so (%d bytes)' % len(loader))

    for name, full in assets:
        out.write(full, name, compress_type=zipfile.ZIP_DEFLATED)
    print('  + %d asset files' % len(assets))

    out.close()
    src.close()
    print('OK -> %s' % a.out)
    print('next: sign it with apksigner / MT Manager')


if __name__ == '__main__':
    main()