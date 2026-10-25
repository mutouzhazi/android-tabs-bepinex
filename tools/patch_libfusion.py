#!/usr/bin/env python3
"""
Adds DT_NEEDED entries to libfusion.so.

The shipped libfusion.so has no RUNPATH. Under the Android linker namespace its
own dependencies are not visible to it, so C++ ABI symbols such as
_ZTVN10__cxxabiv120__si_class_type_infoE fail to resolve even when the library
is already loaded. Listing them in DT_NEEDED pulls them into the same linker
group and the symbols resolve again.

Usage:
  python3 patch_libfusion.py libfusion.so libfusion_patched.so [extra_lib ...]

Requires: pip install lief
"""
import sys

try:
    import lief
except ImportError:
    sys.exit('lief is required: pip install lief')

DEFAULT_LIBS = ['libc++_shared.so', 'libcoreclr.so', 'libtabsopt.so']


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    libs = sys.argv[3:] or DEFAULT_LIBS

    b = lief.parse(src)
    if b is None:
        sys.exit('cannot parse: ' + src)

    have = [str(x) for x in b.libraries]
    print('before:', have)
    for l in libs:
        if l not in have:
            b.add_library(l)
            print('  + DT_NEEDED', l)
    b.write(dst)
    print('written ->', dst)

    chk = lief.parse(dst)
    print('after :', [str(x) for x in chk.libraries])


if __name__ == '__main__':
    main()