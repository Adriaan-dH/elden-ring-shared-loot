"""Build a Windows diagnostic DLL from WSL without a system compiler install."""
import hashlib
import json
import pathlib
import subprocess
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
VERSION = "0.15.2"
cache = ROOT / "work" / "toolchains"
cache.mkdir(parents=True, exist_ok=True)
zig = cache / f"zig-x86_64-linux-{VERSION}" / "zig"
if not zig.exists():
    with urllib.request.urlopen("https://ziglang.org/download/index.json", timeout=30) as response:
        release = json.load(response)[VERSION]["x86_64-linux"]
    archive = cache / "zig.tar.xz"
    print(f"Downloading Zig {VERSION} from ziglang.org", flush=True)
    with urllib.request.urlopen(release["tarball"], timeout=60) as response:
        archive.write_bytes(response.read())
    if hashlib.sha256(archive.read_bytes()).hexdigest() != release["shasum"]:
        raise SystemExit("Toolchain checksum mismatch")
    with tarfile.open(archive) as package:
        package.extractall(cache, filter="data")
output = ROOT / "build-windows"
output.mkdir(exist_ok=True)
base = [str(zig), "c++", "-target", "x86_64-windows-gnu", "-std=c++17",
        "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-Iinclude"]
minhook = ROOT / "third_party" / "minhook"
if not (minhook / "upstream.json").exists():
    raise SystemExit("MinHook sources missing; run python3 scripts/vendor_minhook.py first")
objects = []
for source in ["src/buffer.c", "src/hook.c", "src/trampoline.c", "src/hde/hde64.c"]:
    obj = output / (pathlib.Path(source).stem + ".o")
    subprocess.run([str(zig), "cc", "-target", "x86_64-windows-gnu", "-std=c11", "-c",
                    str(minhook / source), "-o", str(obj)], cwd=ROOT, check=True)
    objects.append(str(obj))
subprocess.run(base + ["-Ithird_party/minhook/include", "-DSL_BUILD", "-shared", "src/shared_loot.cpp",
                       "src/diagnostics_win.cpp", "src/rowa_transport.cpp", "src/session_router.cpp", "src/inventory_probe.cpp", "src/steam_bridge_win.cpp", *objects, "-lbcrypt", "-lws2_32",
                       "-o", str(output / "SharedLoot.dll")], cwd=ROOT, check=True)
subprocess.run(base + ["tests/core_tests.cpp", str(output / "shared_loot.lib"),
                       "-o", str(output / "core_tests.exe")], cwd=ROOT, check=True)
subprocess.run(base + ["tests/packet_tests.cpp", "-o", str(output / "packet_tests.exe")], cwd=ROOT, check=True)
subprocess.run(base + ["tests/source_filter_tests.cpp", "src/session_router.cpp", "-o", str(output / "source_filter_tests.exe")], cwd=ROOT, check=True)
subprocess.run(base + ["tests/session_router_tests.cpp", "src/session_router.cpp", "-o", str(output / "session_router_tests.exe")], cwd=ROOT, check=True)
subprocess.run(base + ["tests/rowa_transport_tests.cpp", "src/rowa_transport.cpp", "-lws2_32", "-o", str(output / "rowa_transport_tests.exe")], cwd=ROOT, check=True)
subprocess.run(base + ["tests/inventory_probe_tests.cpp", "src/inventory_probe.cpp", "src/session_router.cpp", "-o", str(output / "inventory_probe_tests.exe")], cwd=ROOT, check=True)
subprocess.run(base + ["tests/delivery_buffer_tests.cpp", "src/session_router.cpp", "-o", str(output / "delivery_buffer_tests.exe")], cwd=ROOT, check=True)
mock = output / "mock-steam"
mock.mkdir(exist_ok=True)
subprocess.run(base + ["-shared", "tests/fake_steam_api.cpp", "src/session_router.cpp", "-o", str(mock / "steam_api64.dll")], cwd=ROOT, check=True)
print(f"Built {output / 'SharedLoot.dll'}", flush=True)
