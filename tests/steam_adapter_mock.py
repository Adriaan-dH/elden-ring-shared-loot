"""Load test-only Steam exports and exercise the production adapter outside the game."""
import ctypes,pathlib,sys
mock_path=pathlib.Path(sys.argv[1]).resolve()
if mock_path.name.lower()!='steam_api64.dll':raise SystemExit('Expected isolated mock Steam DLL')
mock=ctypes.CDLL(str(mock_path))
library=ctypes.CDLL(str(pathlib.Path(sys.argv[2]).resolve()))
test=library.SL_SteamAdapterMockTest;test.argtypes=[];test.restype=ctypes.c_int
result=test()
if result:raise SystemExit(f'Steam adapter mock failed: {result}')
print('PASS: native Steam export resolution, lobby callback/member discovery, identity messaging, handshake and bidirectional item bundles')
