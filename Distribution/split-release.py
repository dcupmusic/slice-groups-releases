#!/usr/bin/env python3
"""Create small update/full installer archives after notarize.py staple-app.
Model assets are immutable: upload them once to v0.40.3, not every release.
Source is GitHub's automatic archive; dependency sources remain in v0.40.2.
"""
from pathlib import Path
import argparse,hashlib,subprocess,shutil,json,time
p=argparse.ArgumentParser();p.add_argument('version');args=p.parse_args()
root=Path(__file__).resolve().parents[3];folder=root/'work/distribution'/args.version;notary=folder/'notarized'
assert json.loads((notary/'app-status.json').read_text())['status']=='Accepted'
for name,suffix in [('Install Slice Groups.app','-Installer'),('update/Install Slice Groups.app','-Update')]:
 app=notary/name
 for bundle in [app,app/'Contents/PlugIns/Slice Groups.vst3']:
  for attempt in range(6):
   result=subprocess.run(['xcrun','stapler','validate',str(bundle)])
   if result.returncode==0:break
   if result.returncode!=68 or attempt==5:result.check_returncode()
   time.sleep(3+attempt)
  subprocess.run(['codesign','--verify','--deep','--strict',str(bundle)],check=True)
 stage=folder/('full-download' if suffix=='-Installer' else 'update-download')
 if stage.exists():shutil.rmtree(stage)
 stage.mkdir();subprocess.run(['ditto',str(app),str(stage/app.name)],check=True)
 (stage/'INSTALL.txt').write_text('Close your audio host. Open Install Slice Groups.app and click Install.\nYour samples and metadata are preserved.\n'+('Full installer: stem models included, no model download needed.\n' if suffix=='-Installer' else 'Lightweight update: reuses installed stem models. Missing weights download on first use. Internet required only if models are missing.\n')+'Apple Silicon, macOS 14+. Signed and notarised by Apple.\nSource: https://github.com/dcupmusic/slice-groups-releases\n')
 archive=folder/f'SliceGroups-{args.version}-AppleSilicon{suffix}.zip'
 if archive.exists():archive.unlink()
 subprocess.run(['ditto','-c','-k','--sequesterRsrc',str(stage),str(archive)],check=True)
files=sorted(folder.glob(f'SliceGroups-{args.version}-AppleSilicon-*.zip'))
(folder/'SHA256SUMS.txt').write_text(''.join(hashlib.file_digest(f.open('rb'),'sha256').hexdigest()+'  '+f.name+'\n' for f in files))
for f in files:print(f.name,round(f.stat().st_size/1024**2,1),'MiB')
