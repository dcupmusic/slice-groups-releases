#!/usr/bin/env python3
"""Package the current macOS VST3 for GitHub. Local packaging only; no Apple upload."""
import hashlib, plistlib, shutil, subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
def run(*args):subprocess.run([str(a) for a in args],check=True)
def package():
    source=ROOT/'outputs/Slice Groups.vst3'
    version=plistlib.loads((source/'Contents/Info.plist').read_bytes())['CFBundleShortVersionString']
    folder=ROOT/'work/distribution'/version;folder.mkdir(parents=True,exist_ok=True)
    stage=folder/'download'
    if stage.exists():shutil.rmtree(stage)
    stage.mkdir();plugin=stage/source.name;run('ditto',source,plugin)
    resources=plugin/'Contents/Resources'
    for name in ('RubberBand-COPYING','libsamplerate-LICENSE.md'):
        shutil.copy2(ROOT/'outputs/SliceGroups/Vendor'/name,resources/name)
    shutil.copy2(ROOT/'work/JUCE/LICENSE.md',resources/'JUCE-LICENSE.md')
    shutil.copy2(folder/'AGPL-3.0.txt',resources/'SliceGroups-LICENSE.txt')
    run('codesign','--force','--sign','-',plugin)
    run('codesign','--verify','--deep','--strict',plugin)
    (stage/'INSTALL.txt').write_text('''Slice Groups — Apple Silicon Mac, macOS 14 or later\n\n1. Close Ableton or your other audio host.\n2. In Finder, choose Go > Go to Folder, and enter:\n   ~/Library/Audio/Plug-Ins/VST3/\n   Create the VST3 folder if needed.\n3. Copy Slice Groups.vst3 into it. Keep the bundle intact.\n4. Restart your host and rescan VST3 plug-ins.\n5. In Slice Groups, add your sample folders.\n\nThis build is ad-hoc signed, not Apple-notarized. If macOS blocks it,\nallow it in System Settings > Privacy & Security. If the host still\nblocks the downloaded plugin, run this in Terminal for this plugin only:\n\nxattr -dr com.apple.quarantine "$HOME/Library/Audio/Plug-Ins/VST3/Slice Groups.vst3"\n\nRestart your host after allowing the plugin.\n\nThe stem models and runtime are included; no Python install is needed.\nNo Windows or Intel Mac build is included.\nSource and downloads: https://github.com/dcupmusic/slice-groups-releases\n''')
    archive=folder/f'SliceGroups-{version}-AppleSilicon.zip'
    if archive.exists():archive.unlink()
    run('ditto','-c','-k','--sequesterRsrc',stage,archive)
    print(archive)
if __name__=='__main__':package()
