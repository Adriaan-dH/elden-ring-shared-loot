"""Package only our observer, its separate profile, instructions, and notices."""
import argparse
import hashlib
import json
import pathlib
import zipfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=pathlib.Path)
parser.add_argument("--grant-test", action="store_true", help="Enable the explicit F8 one-shot Rowa test")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
files = {
    "shared-loot/SharedLoot.dll": ROOT / "build-windows/SharedLoot.dll",
    "shared-loot/SharedLoot.ini": ROOT / "packaging/SharedLoot.ini",
    "shared-loot/MANUAL-TEST.md": ROOT / ("docs/grant-test.md" if args.grant_test else "docs/manual-test.md"),
    "shared-loot/THIRD-PARTY-NOTICES.txt": ROOT / "third_party/minhook/LICENSE.txt",
    "config_sharedloot_diagnostics.toml": ROOT / "packaging/config_sharedloot_diagnostics.toml",
    "launch_sharedloot_diagnostics.bat": ROOT / "packaging/launch_sharedloot_diagnostics.bat",
}
manifest = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name,path in files.items()}
archive = args.output / ("SharedLoot-grant-test-0.2.0.zip" if args.grant_test else "SharedLoot-observer-0.2.0.zip")
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED) as package:
    for name,path in files.items():
        content = path.read_bytes()
        if name.endswith("SharedLoot.ini"):
            content += b"\n[grant_test]\nenabled=" + (b"1" if args.grant_test else b"0") + b"\n"
        if name.endswith(".bat"):
            content = content.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
        package.writestr(name, content)
        manifest[name] = hashlib.sha256(content).hexdigest()
    package.writestr("shared-loot/manifest.json", json.dumps(manifest, indent=2)+"\n")
(args.output / ("GRANT-TEST.md" if args.grant_test else "MANUAL-TEST.md")).write_bytes(files["shared-loot/MANUAL-TEST.md"].read_bytes())
print(archive)
print("SHA-256", hashlib.sha256(archive.read_bytes()).hexdigest())
