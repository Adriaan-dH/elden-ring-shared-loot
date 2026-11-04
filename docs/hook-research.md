# Acquisition observer research

Investigated on 2026-11-04 without opening the game.

## Local executable findings

Windows file/product version: `2.7.2.0`. File size: 86,973,008 bytes. SHA-256: `f9118fea2cfd8fa3901e8060bfadb1552cb2471771ded8a5b662c4e298ffb328`.

Static inspection found exactly one `mov eax,[rdx]; cmp eax,0xA` sequence (`8B 02 83 F8 0A`) in executable `.text` sections. The PE exception-function table places it inside RVA `0x5618B0`–`0x561E1D`; the sequence is at function offset `0x52`. The entry is a normal x64 function prologue. `scripts/inspect_game.py` checks these relationships and generates the profile; it does not modify the executable.

Disassembly of this function shows:

- `RCX` is retained as a manager pointer.
- `RDX` points to a count followed by entries. Counts above 10 skip processing.
- Entries start at packet offset 4 and are processed with a 16-byte stride. Four 32-bit fields are copied per entry.
- `R8` is another pointer and `R9D` a 32-bit argument. Their meanings are not assumed by the observer.
- The code performs capacity/quantity processing and can split or reject requested quantities. Therefore a captured packet is logged as `award_request`, not a successful inventory delta.

The prototype preserves the observed four arguments and raw integer return register, then forwards to the original function through a MinHook trampoline. It copies only readable packet bytes using `ReadProcessMemory` on its own process and bounds counts to 10. It does not dereference manager/output pointers, call an item-grant function independently, interpret source categories, or modify packet contents.

The hook captures the immediate caller's module and relative address to help compare source paths during the first manual test. If different source types converge on the same caller, deeper call-path investigation will be necessary. Other acquisition paths may bypass this function entirely.

## Compatibility boundary

The full executable hash, PE timestamp/image size, and first 32 function bytes must match the inspected build. If Seamless or another component changes those entry bytes before initialization, the observer refuses attachment. It does not force hook chaining. A three-second initialization delay is only a practical startup delay, not a guarantee of Seamless readiness or future hook compatibility.

MinHook necessarily patches executable code in memory. No game files or inventory/world state are deliberately written by the observer. Synthetic native tests validate the detour on a test function; they cannot prove the inferred game ABI or real co-existence. The DLL remains pinned for the process lifetime once its worker starts, preventing accidental unloading of active callbacks. Runtime detach/hot reload is not supported.

## Research sources and dependencies

- https://github.com/kh0nsu/EldenRingTool/blob/main/ERProcess.cs exposes an item-spawn signature and packet-writing behavior that guided the candidate search. The installed executable's function boundary, argument use, and loop stride were then independently inspected with GNU objdump. No item-spawning patch template or third-party tool code was incorporated into the observer.
- https://github.com/vswarte/fromsoftware-rs/blob/main/crates/eldenring/src/cs/item.rs currently exposes `MapItemMan` without an implemented layout. It does not supply a ready-made pickup API for this task.
- https://github.com/TsudaKageyu/minhook/tree/v1.3.4 supplies the native hook implementation. Unmodified source is vendored at commit `c3fcafdc10146beb5919319d0683e44e3c30d537`; its complete notices are included in the test package.

No Seamless binaries, game assets, save files, settings, or passwords are included in the package.
