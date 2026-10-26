#!/usr/bin/env python3
"""Build the d2xq touch component into the XNU tree.

Mirrors the other UniXNU `build-*.py` archive builders: it compiles the
component sources from the assembled tree and emits a static archive.

Usage (called by UniXNU `[kernel].archives`):
    build-d2xq-touch.py
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
# In the assembled build tree this script sits in scripts/ and the component is
# at drivers/NativeSecTs; when run in place, use the script own directory.
_CWD_DRIVER = Path("drivers") / "NativeSecTs"
DRIVER_DIR = _CWD_DRIVER if _CWD_DRIVER.is_dir() else ROOT
OUT_DIR = Path("build") / "native-sects"

CFLAGS = [
    "--target=aarch64-none-elf",
    "-std=c11",
    "-ffreestanding",
    "-fno-builtin",
    "-Wall",
    "-Wextra",
]


def main() -> int:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    sources = sorted(DRIVER_DIR.glob("*.c"))
    if not sources:
        print("no sources in", DRIVER_DIR, file=sys.stderr)
        return 1

    objects = []
    for src in sources:
        obj = OUT_DIR / (src.stem + ".o")
        cmd = ["clang", *CFLAGS, "-I", str(DRIVER_DIR), "-c", str(src), "-o", str(obj)]
        print(" ".join(cmd))
        subprocess.check_call(cmd)
        objects.append(obj)

    archive = OUT_DIR / "libNativeSecTs.a"
    if archive.exists():
        archive.unlink()
    subprocess.check_call(["llvm-ar", "rcs", str(archive)] + [str(o) for o in objects])
    print("archive:", archive)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
