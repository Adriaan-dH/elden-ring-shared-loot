# Development

The active Steam implementation is in `src/session_router.cpp`, `src/steam_bridge_win.cpp`, `src/diagnostics_win.cpp`, `src/inventory_probe.cpp`, and `include/source_filter.h`. `shared_loot.cpp` exposes an earlier portable C API for tests; `rowa_transport.cpp` and related scripts retain disabled LAN/Rowa diagnostic experiments. Historical investigation notes are under `docs/`.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
python3 scripts/build_windows.py
python3 scripts/package_release.py dist
```

The Windows helper runs from Linux/WSL, downloads pinned Zig 0.15.2 if needed, and checks its upstream checksum. MinHook 1.3.4 is vendored with attribution. Format first-party C++ with `.clang-format`; do not reformat vendored sources.

Run the generated native Windows test executables and `tests/windows_diagnostics.py <DLL> [eldenring.exe]`. The isolated fake Steam adapter is tested with `tests/steam_adapter_mock.py <mock-steam/steam_api64.dll> <SharedLoot.dll>`. Never install or distribute the fake Steam library with the game.

`package_release.py` is the current public packaging command. Older observer/LAN/Steam-test packagers are development history, not the public release workflow. Public packages include only the addon DLL, INI, README and combined license attribution. Keep logs, saves, generated seed files, Seamless binaries/settings, build outputs, private tokens and machine-specific configuration out of Git.

For executable updates, review disassembly and verify the hook and frame-task ABI before changing profiles. Do not remove build guards just to allow a different executable. Automated tests cannot establish in-game compatibility; record live test evidence separately.
