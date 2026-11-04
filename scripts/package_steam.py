"""Package one identical Steam addon for every PC; no local credentials/config."""
import argparse,hashlib,json,pathlib,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=pathlib.Path);args=parser.parse_args()
args.output.mkdir(parents=True,exist_ok=True)
sources={
 'shared-loot/SharedLoot.dll':'build-windows/SharedLoot.dll',
 'shared-loot/SharedLoot.ini':'packaging/SharedLoot.steam.ini',
 'shared-loot/STEAM-TEST.md':'docs/steam-test.md',
 'shared-loot/THIRD-PARTY-NOTICES.txt':'third_party/minhook/LICENSE.txt',
 'config_sharedloot_steam.toml':'packaging/config_sharedloot_steam.toml',
 'launch_sharedloot_steam.bat':'packaging/launch_sharedloot_steam.bat'}
files={name:(ROOT/path).read_bytes() for name,path in sources.items()}
for name in files:
 if name.endswith('.bat'):files[name]=files[name].replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')
files['shared-loot/manifest.json']=(json.dumps({name:hashlib.sha256(data).hexdigest() for name,data in files.items()},indent=2)+'\n').encode()
archive=args.output/'SharedLoot-Steam-0.4.1.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED) as package:
 for name,data in files.items():package.writestr(name,data)
(args.output/'STEAM-TEST.md').write_bytes((ROOT/'docs/steam-test.md').read_bytes())
print(archive)
