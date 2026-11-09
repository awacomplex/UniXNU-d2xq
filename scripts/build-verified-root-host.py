#!/usr/bin/env python3
"""Build the VerifiedRoot host artefacts the policy builders need.

Creates build/verified-root-host/keys/{owner.pem,owner-public.pem} (RSA-3072
owner anchor) and build/verified-root-host/verified-root.dylib (the real C
signature/block verifier linked against the host OpenSSL), matching what
UniXNU's kits/VerifiedRoot/tests/check.py does for its host round trip.
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--tree", type=Path, required=True)
parser.add_argument("--openssl-prefix", type=Path)
args = parser.parse_args()

tree = args.tree.resolve()
kit = tree / "kits/VerifiedRoot"
if not (kit / "src/verified_root.c").is_file():
    sys.exit("Require the UniXNU kits/VerifiedRoot sources")

out = tree / "build/verified-root-host"
keys = out / "keys"
keys.mkdir(parents=True, exist_ok=True)
private = keys / "owner.pem"
public = keys / "owner-public.pem"
if not private.exists() or not public.exists():
    subprocess.run(["openssl", "genpkey", "-algorithm", "RSA",
                    "-pkeyopt", "rsa_keygen_bits:3072", "-out", str(private)], check=True)
    os.chmod(private, 0o600)
    subprocess.run(["openssl", "pkey", "-in", str(private), "-pubout", "-out", str(public)], check=True)

prefix = args.openssl_prefix
if prefix is None:
    for candidate in ("/opt/homebrew/opt/openssl@3", "/usr/local/opt/openssl@3"):
        if Path(candidate).is_dir():
            prefix = Path(candidate)
            break
if prefix is None:
    sys.exit("Pass --openssl-prefix (Homebrew openssl) or install openssl@3")

library = out / "verified-root.dylib"
subprocess.run(["xcrun", "clang", "-std=c11", "-Wall", "-Wextra", "-Werror", "-dynamiclib",
                "-I", str(kit / "include"), "-I", str(prefix / "include"),
                str(kit / "src/verified_root.c"), str(kit / "tests/openssl_provider.c"),
                "-L", str(prefix / "lib"), "-lcrypto", "-o", str(library)], check=True)
print("host keys:", private, public)
print("host library:", library)
