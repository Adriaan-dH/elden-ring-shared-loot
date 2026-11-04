"""Build the slim addon-only archive for the experimental GitHub release."""
import argparse
from pathlib import Path
import struct
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.4.1"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    dll = (ROOT / "build-windows/SharedLoot.dll").read_bytes()
    if len(dll) < 64 or dll[:2] != b"MZ":
        raise SystemExit("Expected a Windows DLL")
    pe = struct.unpack_from("<I", dll, 0x3c)[0]
    if (pe + 6 > len(dll) or dll[pe:pe+4] != b"PE\0\0"
            or struct.unpack_from("<H", dll, pe+4)[0] != 0x8664):
        raise SystemExit("Expected an x64 PE DLL")
    # Build from an explicit list: never collect runtime logs, mock libraries,
    # local credentials, or third-party game/mod assets by walking a directory.
    notices = ((ROOT / "LICENSE").read_bytes()
               + b"\n--- Third-party attribution: MinHook ---\n\n"
               + (ROOT / "third_party/minhook/LICENSE.txt").read_bytes())
    files = {
        "shared-loot/SharedLoot.dll": dll,
        "shared-loot/SharedLoot.ini": (ROOT / "packaging/SharedLoot.steam.ini").read_bytes(),
        "shared-loot/README.md": (ROOT / "README.md").read_bytes(),
        "shared-loot/LICENSE.txt": notices,
    }
    args.output.mkdir(parents=True, exist_ok=True)
    archive = args.output / f"SharedLoot-{VERSION}-experimental.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
        for name, data in files.items():
            package.writestr(name, data)
    # Check the actual output bytes; no hash manifest is shipped to users.
    with zipfile.ZipFile(archive) as package:
        if set(package.namelist()) != set(files) or package.testzip() is not None:
            raise SystemExit("Release archive validation failed")
        for name, data in files.items():
            if package.read(name) != data:
                raise SystemExit(f"Release file mismatch: {name}")
    print(archive)


if __name__ == "__main__":
    main()
