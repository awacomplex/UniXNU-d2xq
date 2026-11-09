#!/usr/bin/env python3
"""Extract and mount the original OS and Cryptex1,SystemOS volumes.

The VerifiedRoot policy builders read the attached raw media of the exact
supplied iPad7,1 iPadOS 17.7.11 / 21H461 images.  This extracts the two IPSW
members into .cache/firmware/target-volumes and attaches them read-only at
os-mount and systemos-mount (idempotent).
"""
import argparse
import hashlib
import os
import plistlib
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

COMPONENTS = (("OS", "os-mount"), ("Cryptex1,SystemOS", "systemos-mount"))

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--tree", type=Path, required=True)
parser.add_argument("--ipsw", type=Path, default=Path(os.environ.get("UNIXNU_IPSW", "")))
args = parser.parse_args()
tree = args.tree.resolve()
ipsw = args.ipsw
if not ipsw or not ipsw.is_file():
    sys.exit("Pass --ipsw or set UNIXNU_IPSW to the iPad7,1 iPadOS 17.7.11 restore")

targets = tree / ".cache/firmware/target-volumes"
targets.mkdir(parents=True, exist_ok=True)

def extract(component):
    with zipfile.ZipFile(ipsw) as archive:
        manifest = plistlib.loads(archive.read("BuildManifest.plist"))
        if (manifest["ProductVersion"] != "17.7.11" or manifest["ProductBuildVersion"] != "21H461"
                or "iPad7,1" not in manifest["SupportedProductTypes"]):
            sys.exit("Use the supplied iPad7,1 iPadOS 17.7.11 / 21H461 input")
        identity = next(item for item in manifest["BuildIdentities"]
                        if item["Info"]["DeviceClass"] == "j120ap"
                        and int(item["ApChipID"], 16) == 0x8011 and int(item["ApBoardID"], 16) == 12)
        member = identity["Manifest"][component]["Info"]["Path"]
        destination = targets / Path(member).name
        if destination.is_file():
            return destination
        temporary = destination.with_suffix(".partial")
        with archive.open(member) as source, temporary.open("xb") as sink:
            shutil.copyfileobj(source, sink, 8 * 1024 * 1024)
        temporary.rename(destination)
        return destination

def attached(path):
    info = plistlib.loads(subprocess.check_output(["hdiutil", "info", "-plist"]))["images"]
    return any(Path(i["image-path"]).resolve() == path.resolve() for i in info)

def mount(image, point, raw):
    if attached(image):
        return
    point.mkdir(parents=True, exist_ok=True)
    command = ["hdiutil", "attach", "-nobrowse", "-readonly", "-mountpoint", str(point)]
    if raw:
        command += ["-imagekey", "diskimage-class=CRawDiskImage"]
    command.append(str(image))
    subprocess.run(command, check=True)

for component, name in COMPONENTS:
    image = extract(component)
    mount(image, targets / name, raw=(component != "OS"))
print("target volumes ready under", targets)
