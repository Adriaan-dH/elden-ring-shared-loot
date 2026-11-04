"""Exercise the native detour against a synthetic function, outside Elden Ring."""
import ctypes
import pathlib
import sys
import tempfile

path = pathlib.Path(sys.argv[1]).resolve()
library = ctypes.CDLL(str(path))
test = library.SL_DiagnosticHookSelfTest
test.argtypes = []
test.restype = ctypes.c_int
for iteration in range(3):
    result = test()
    if result != 0:
        raise SystemExit(f"Hook self-test iteration {iteration} failed: {result}")
print("PASS: native MinHook detour, argument/return preservation, packet snapshot, repeated enable/disable")
grant_test = library.SL_DiagnosticGrantSelfTest
grant_test.argtypes = []
grant_test.restype = ctypes.c_int
if grant_test() != 0:
    raise SystemExit("Diagnostic grant routing test failed")
print("PASS: one-shot diagnostic grant, scripted reward exclusion, quantity/metadata, recursion guard, unchanged source packet")
network_grant = library.SL_DiagnosticNetworkGrantSelfTest
network_grant.argtypes = []
network_grant.restype = ctypes.c_int
if network_grant() != 0:
    raise SystemExit("Remote Rowa grant adapter test failed")
print("PASS: remote Rowa quantity, fixed item allowlist, callback exclusions, unchanged triggering pickup")
abi_test = library.SL_FrameAndSteamAbiSelfTest
abi_test.argtypes = []
abi_test.restype = ctypes.c_int
if abi_test() != 0:
    raise SystemExit("Steam callback/frame task ABI test failed")
print("PASS: Windows Steam callback vtable and persistent frame-task ABI")
broad_test = library.SL_BroadGrantSelfTest
broad_test.argtypes = []
broad_test.restype = ctypes.c_int
if broad_test() != 0:
    raise SystemExit("Broad item grant adapter test failed")
print("PASS: broad item bundle grant, equipment metadata, generic notification context, guarded execution")
check = library.SL_DiagnosticFileMatches
check.argtypes = [ctypes.c_wchar_p]
check.restype = ctypes.c_int
if check(None) != 0:
    raise SystemExit("Null executable path accepted")
with tempfile.TemporaryDirectory(prefix="sharedloot-test-") as directory:
    fixture = pathlib.Path(directory) / "not-a-game.bin"
    fixture.write_bytes(b"invalid game build")
    if check(str(fixture)) != 0:
        raise SystemExit("Mismatching executable accepted")
if len(sys.argv) > 2:
    if check(str(pathlib.Path(sys.argv[2]).resolve())) != 1:
        raise SystemExit("Installed executable failed the Windows SHA-256 profile check")
    print("PASS: read-only Windows executable hash verification and mismatch rejection")
