"""Build matching host/client packages; keep the generated token out of Git."""
import argparse,hashlib,json,pathlib,secrets,zipfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('output',type=pathlib.Path)
parser.add_argument('--host-address',required=True)
args=parser.parse_args()
import ipaddress
address=ipaddress.IPv4Address(args.host_address)
if not address.is_private or address.is_loopback:raise SystemExit('Use the host PC private LAN IPv4 address')
args.output.mkdir(parents=True,exist_ok=True)
secret=ROOT/'work/rowa-lan-token.txt';secret.parent.mkdir(exist_ok=True)
if not secret.exists():secret.write_text(secrets.token_hex(32)+'\n')
token=secret.read_text().strip()
if len(token)!=64 or any(c not in '0123456789abcdef' for c in token):raise SystemExit('Invalid local token file')
for role in ('host','client'):
    config=(f'; SharedLoot 0.3.0 - Rowa-only LAN test\n[diagnostics]\nenabled=1\n'
            f'\n[grant_test]\nenabled=0\n\n[network]\nenabled=1\nrole={role}\n'
            f'address={address}\nport=49231\ntoken={token}\nsend_pickups={1 if role=="host" else 0}\n')
    files={
        'shared-loot/SharedLoot.dll':(ROOT/'build-windows/SharedLoot.dll').read_bytes(),
        'shared-loot/SharedLoot.ini':config.encode(),
        'shared-loot/MANUAL-TEST.md':(ROOT/'docs/two-pc-test.md').read_bytes(),
        'shared-loot/THIRD-PARTY-NOTICES.txt':(ROOT/'third_party/minhook/LICENSE.txt').read_bytes(),
        'config_sharedloot_diagnostics.toml':(ROOT/'packaging/config_sharedloot_diagnostics.toml').read_bytes(),
        'launch_sharedloot_diagnostics.bat':(ROOT/'packaging/launch_sharedloot_diagnostics.bat').read_bytes().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n')}
    manifest={name:hashlib.sha256(data).hexdigest() for name,data in files.items()}
    files['shared-loot/manifest.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    path=args.output/f'SharedLoot-{role}-0.3.0.zip'
    with zipfile.ZipFile(path,'w',zipfile.ZIP_DEFLATED) as package:
        for name,data in files.items():package.writestr(name,data)
    print(path)
(args.output/'TWO-PC-TEST.md').write_bytes((ROOT/'docs/two-pc-test.md').read_bytes())
