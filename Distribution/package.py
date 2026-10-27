#!/usr/bin/env python3
"""Build a system-wide VST3 PKG. Signing requires Developer ID Installer.

Input must be the complete, Developer ID signed plug-in (including models).
An unsigned preparation package is explicitly named UNSIGNED and must never
be published. Notarization is performed separately, after signing.
"""
import argparse
import plistlib
import shutil
import subprocess
from pathlib import Path
from xml.sax.saxutils import escape

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--plugin', type=Path, required=True)
p.add_argument('--version', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--identity', help='Developer ID Installer identity; omit to prepare only')
a = p.parse_args()
source = a.plugin.resolve()
folder = a.output.resolve()
folder.mkdir(parents=True, exist_ok=True)
info = plistlib.loads((source/'Contents/Info.plist').read_bytes())
assert info['CFBundleIdentifier'] == 'audio.slicegroups.plugin'
assert info['CFBundleShortVersionString'] == a.version
subprocess.run(['codesign', '--verify', '--deep', '--strict', str(source)], check=True)
subprocess.run(['codesign', '--verify', '-R', '=anchor apple generic and certificate leaf[subject.OU] = "Q5YNM8U8JB"', str(source)], check=True)
for model in ['SCNet.pt', 'SCNet-HQ.pt']:
    assert list(source.rglob(model)), f'Full installer needs {model}'
root = folder/'payload'
if root.exists():
    shutil.rmtree(root)
root.mkdir()
subprocess.run(['ditto', str(source), str(root/'Slice Groups.vst3')], check=True)
components = folder/'components.plist'
subprocess.run(['pkgbuild', '--analyze', '--root', str(root), str(components)], check=True)
entries = plistlib.loads(components.read_bytes())
for entry in entries:
    entry.update(BundleIsRelocatable=False, BundleHasStrictIdentifier=True,
                 BundleIsVersionChecked=True, BundleOverwriteAction='upgrade')
components.write_bytes(plistlib.dumps(entries))
scripts = folder/'scripts'
scripts.mkdir(exist_ok=True)
preinstall = scripts/'preinstall'
preinstall.write_text('''#!/bin/sh
# Never replace a plug-in while a recognised audio host is using it.
for host in Live REAPER Logic\\ Pro GarageBand BitwigStudio Cubase Nuendo Studio\\ One; do
    if /usr/bin/pgrep -x "$host" >/dev/null 2>&1; then
        echo "Close $host before installing Slice Groups." >&2
        exit 1
    fi
done
exit 0
''')
preinstall.chmod(0o755)
component = folder/'SliceGroups-component.pkg'
subprocess.run(['pkgbuild', '--root', str(root), '--component-plist', str(components),
                '--identifier', 'audio.slicegroups.pkg.vst3', '--version', a.version,
                '--install-location', '/Library/Audio/Plug-Ins/VST3', '--ownership', 'recommended',
                '--scripts', str(scripts), str(component)], check=True)
resources = folder/'resources'
resources.mkdir(exist_ok=True)
(resources/'Welcome.html').write_text('''<html><body><h2>Slice Groups '''+escape(a.version)+'''</h2>
<p>Apple Silicon &middot; macOS 14 or later</p>
<p>Close Ableton Live and other audio hosts before installing.</p>
<p>This installs the VST3 plug-in and all stem models for all users into
<b>/Library/Audio/Plug-Ins/VST3/Slice Groups.vst3</b>. macOS will request administrator authorization.</p>
<p>Your samples, libraries, tags, presets and settings are preserved.</p>
<p>If you previously installed a second copy in your personal
~/Library/Audio/Plug-Ins/VST3 folder, remove that duplicate before restarting your host.</p>
</body></html>''')
distribution = folder/'Distribution.xml'
distribution.write_text('''<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
  <title>Slice Groups '''+escape(a.version)+'''</title>
  <welcome file="Welcome.html" mime-type="text/html"/>
  <options customize="never" require-scripts="false" hostArchitectures="arm64"/>
  <domains enable_localSystem="true" enable_currentUserHome="false" enable_anywhere="false"/>
  <volume-check><allowed-os-versions><os-version min="14.0"/></allowed-os-versions></volume-check>
  <choices-outline><line choice="vst3"/></choices-outline>
  <choice id="vst3" visible="false"><pkg-ref id="audio.slicegroups.pkg.vst3"/></choice>
  <pkg-ref id="audio.slicegroups.pkg.vst3" version="'''+escape(a.version)+'''" onConclusion="none">SliceGroups-component.pkg</pkg-ref>
</installer-gui-script>
''')
name = f'SliceGroups-{a.version}-AppleSilicon'+('' if a.identity else '-UNSIGNED')+'.pkg'
command = ['productbuild', '--distribution', str(distribution), '--resources', str(resources),
           '--package-path', str(folder)]
if a.identity:
    command += ['--sign', a.identity, '--timestamp']
subprocess.run(command+[str(folder/name)], check=True)
print(folder/name)
