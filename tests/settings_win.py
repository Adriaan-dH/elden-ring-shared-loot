"""Native INI parsing and genuinely disabled file logging; no game launch."""
import ctypes,json,pathlib,sys,tempfile
library=ctypes.CDLL(str(pathlib.Path(sys.argv[1]).resolve()))
read=library.SL_ReadSettingsFile
read.argtypes=[ctypes.c_wchar_p];read.restype=ctypes.c_int
sink=library.SL_TestLoggingSink
sink.argtypes=[ctypes.c_wchar_p,ctypes.c_int];sink.restype=ctypes.c_int
assert read(None)==6
with tempfile.TemporaryDirectory(prefix="sharedloot-settings-") as tmp:
 root=pathlib.Path(tmp);ini=root/"SharedLoot.ini"
 assert read(str(ini))==6
 ini.write_text("[logging]\nenabled=0\n[sharing]\nshop_purchases=0\n",encoding="utf-8")
 assert read(str(ini))==4
 ini.write_text("; testing config\n[logging]\nenabled=1\n[sharing]\nshop_purchases=1\ngathering_materials=0\n",encoding="utf-8")
 assert read(str(ini))==3
 ini.write_text("[diagnostics]\nenabled=1\n",encoding="utf-8")
 assert read(str(ini))==7 # migration keeps local legacy logging on
 ini.write_text("[logging]\nenabled=0\n[diagnostics]\nenabled=1\n",encoding="utf-8")
 assert read(str(ini))==6 # explicit new setting wins
 folder=root/"mod";folder.mkdir()
 assert sink(str(folder),0)==0 and not (folder/"logs").exists()
 assert sink(str(folder),1)==1
 logs=list((folder/"logs").glob("*.jsonl"));assert len(logs)==1
 assert json.loads(logs[0].read_text())=={"event":"logging_self_test"}
 before=logs[0].read_bytes();assert sink(str(folder),0)==0
 assert logs[0].read_bytes()==before
 blocked=root/"not-a-directory";blocked.write_text("fixture")
 assert sink(str(blocked),1)==0 # logging failures are non-fatal
print("PASS: INI defaults/overrides, legacy logging migration, logging-off creates no files, enabled logging and non-fatal file errors")

check=library.SL_ShopHookSelfTest
check.restype=ctypes.c_int
assert check()==0
print("PASS: native shop transaction hook, missing-probe fallthrough, argument/return preservation and recursion guard cleanup")
