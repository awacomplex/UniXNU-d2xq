#!/usr/bin/env python3
"""Give the storage archives the object names the kernel link re-audit expects.

build-xnu-4k-baseline.sh re-hashes <archive-dir>/<archive-stem-minus-lib>.o
against bindings.json before linking.  The compiled objects keep their source
names, so copy them byte for byte under the audited names.
"""
import shutil
import sys
from pathlib import Path

tree = Path(sys.argv[sys.argv.index("--tree") + 1]).resolve() if "--tree" in sys.argv else Path.cwd()
pairs = (
    (tree / "build/virtio-mmio-block-verified-bundle/NativeVirtioBlock.o",
     tree / "build/virtio-mmio-block-verified-bundle/VirtioMmioBlock.o"),
    (tree / "build/ram-block/NativeRamBlock.o",
     tree / "build/ram-block/RamBlock.o"),
)
for source, destination in pairs:
    if not source.is_file():
        sys.exit(f"missing compiled object {source}")
    shutil.copyfile(source, destination)
print("audited object names:", ", ".join(p[1].name for p in pairs))
