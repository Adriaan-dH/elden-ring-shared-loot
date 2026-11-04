"""Fetch unmodified MinHook 1.3.4 sources with their license for native builds."""
import json
import pathlib
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
TARGET = ROOT / "third_party" / "minhook"
FILES = ["LICENSE.txt", "include/MinHook.h", "src/buffer.c", "src/buffer.h",
         "src/hook.c", "src/trampoline.c", "src/trampoline.h",
         "src/hde/hde64.c", "src/hde/hde64.h", "src/hde/table64.h", "src/hde/pstdint.h"]
request = urllib.request.Request(
    "https://api.github.com/repos/TsudaKageyu/minhook/git/ref/tags/v1.3.4",
    headers={"User-Agent": "elden-ring-shared-loot-build"})
with urllib.request.urlopen(request, timeout=30) as response:
    reference = json.load(response)["object"]
if reference["type"] != "commit":
    raise SystemExit("Expected a lightweight tag; inspect upstream before updating")
commit = reference["sha"]
TARGET.mkdir(parents=True, exist_ok=True)
lock = TARGET / "upstream.json"
if lock.exists() and json.loads(lock.read_text())["commit"] != commit:
    raise SystemExit("Upstream tag changed; refusing to replace pinned sources")
for name in FILES:
    destination = TARGET / name
    destination.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(
            f"https://raw.githubusercontent.com/TsudaKageyu/minhook/{commit}/{name}", timeout=30) as response:
        destination.write_bytes(response.read())
lock.write_text(json.dumps({"repository": "https://github.com/TsudaKageyu/minhook",
                            "tag": "v1.3.4", "commit": commit}, indent=2) + "\n")
print(f"Vendored MinHook v1.3.4 at {commit}")
