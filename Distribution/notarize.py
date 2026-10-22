#!/usr/bin/env python3
"""Developer ID distribution. Requires a local signing identity and notarytool Keychain profile.
No credentials are read or exported. No installed plug-ins or sample libraries are changed.
Run each phase independently so submissions can be resumed after interruption.
"""
import argparse, hashlib, json, plistlib, shutil, subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
IDENTITY = 'Developer ID Application: DUNCAN MACLENNAN (Q5YNM8U8JB)'
MACH = (b'\xcf\xfa\xed\xfe', b'\xce\xfa\xed\xfe', b'\xca\xfe\xba\xbe', b'\xca\xfe\xba\xbf')
def run(*args, capture=False):
    args = [str(a) for a in args]
    print('+', ' '.join(args), flush=True)
    return subprocess.check_output(args, text=True) if capture else subprocess.check_call(args)
def write_json(path, value): path.write_text(json.dumps(value, indent=2)+'\n')
def sign(path): run('codesign', '--force', '--sign', IDENTITY, '--options', 'runtime', '--timestamp', path)
def verify(path): run('codesign', '--verify', '--deep', '--strict', '--verbose=2', path)
def sign_tree(bundle):
    for file in sorted(bundle.rglob('*')):
        if file.is_file() and not file.is_symlink():
            with file.open('rb') as source: magic = source.read(4)
            if magic in MACH: sign(file)
    nested = [p for p in bundle.rglob('*') if p.is_dir() and not p.is_symlink() and p.suffix in ('.vst3','.app','.framework','.xpc')]
    for child in sorted(nested, key=lambda p:len(p.parts), reverse=True): sign(child)
    sign(bundle); verify(bundle)
def submit(file, name):
    receipt = folder/(name+'-submission.json')
    if receipt.exists(): raise RuntimeError('Submission already exists; use status-'+name+' rather than resubmitting.')
    result = json.loads(run('xcrun','notarytool','submit',file,'--keychain-profile',args.profile,'--output-format','json',capture=True))
    write_json(receipt,result)
    print('Saved submission:',result.get('id'))
def status(name):
    receipt=json.loads((folder/(name+'-submission.json')).read_text())
    result=json.loads(run('xcrun','notarytool','info',receipt['id'],'--keychain-profile',args.profile,'--output-format','json',capture=True))
    write_json(folder/(name+'-status.json'),result);print(json.dumps(result,indent=2))
    if result['status']!='In Progress':
        run('xcrun','notarytool','log',receipt['id'],'--keychain-profile',args.profile,folder/(name+'-apple-log.json'))
    return result['status']
def accepted(name):
    result=json.loads((folder/(name+'-status.json')).read_text())
    if result.get('status')!='Accepted': raise RuntimeError(name+' is not yet accepted by Apple.')
def archive(source, target, keep_parent=False):
    if target.exists():target.unlink()
    command=['ditto','-c','-k','--sequesterRsrc']
    if keep_parent:command+=['--keepParent']
    run(*command,source,target)

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('phase',choices=['prepare','submit-app','status-app','staple-app','make-dmg','submit-dmg','status-dmg','finish'])
parser.add_argument('--version',default='0.38.0')
parser.add_argument('--profile',default='BounceLabNotary')
args=parser.parse_args()
folder=ROOT/'work/distribution'/args.version/'notarized';folder.mkdir(parents=True,exist_ok=True)
app=folder/'Install Slice Groups.app';payload=app/'Contents/PlugIns/Slice Groups.vst3'
dmg=folder/f'SliceGroups-{args.version}-AppleSilicon.dmg'
if args.phase=='prepare':
    source=ROOT/'work/distribution'/args.version/'download/Slice Groups.vst3'
    if not source.exists():raise RuntimeError('Run Distribution/release.py first to stage the matching build and licenses.')
    info=plistlib.loads((source/'Contents/Info.plist').read_bytes())
    assert info['CFBundleShortVersionString']==args.version
    if app.exists():raise RuntimeError('Staged app already exists; remove it deliberately before re-preparing.')
    payload.parent.mkdir(parents=True);run('ditto',source,payload)
    macros=app/'Contents/MacOS';macros.mkdir()
    run('xcrun','swiftc','-module-cache-path',folder/'swift-cache','-O','-target','arm64-apple-macos14.0','-framework','AppKit','-framework','Security',ROOT/'outputs/SliceGroups/Distribution/Installer.swift','-o',macros/'InstallSliceGroups')
    plist={'CFBundleExecutable':'InstallSliceGroups','CFBundleIdentifier':'audio.slicegroups.installer','CFBundleName':'Install Slice Groups','CFBundleDisplayName':'Install Slice Groups','CFBundleShortVersionString':args.version,'CFBundleVersion':args.version,'CFBundlePackageType':'APPL','LSMinimumSystemVersion':'14.0','NSHighResolutionCapable':True,'NSHumanReadableCopyright':'Slice Groups'}
    (app/'Contents/Info.plist').write_bytes(plistlib.dumps(plist))
    sign_tree(app)
    # Both variants receive independent signatures/tickets in one submission.
    update=folder/'update/Install Slice Groups.app'
    update.parent.mkdir(exist_ok=True);run('ditto',app,update)
    for name in ['SCNet.pt','SCNet-HQ.pt']:(update/'Contents/PlugIns/Slice Groups.vst3/Contents/Resources'/name).unlink(missing_ok=True)
    sign_tree(update)
    submission=folder/'submission';submission.mkdir(exist_ok=True)
    run('ditto',app,submission/'full/Install Slice Groups.app')
    run('ditto',update,submission/'update/Install Slice Groups.app')
    archive(submission,folder/'apple-submission.zip')
elif args.phase=='submit-app': submit(folder/'apple-submission.zip','app')
elif args.phase=='status-app': status('app')
elif args.phase=='staple-app':
    accepted('app');run('xcrun','stapler','staple',payload);run('xcrun','stapler','validate',payload)
    run('xcrun','stapler','staple',app);run('xcrun','stapler','validate',app);verify(app)
    update=folder/'update/Install Slice Groups.app'
    for bundle in [update/'Contents/PlugIns/Slice Groups.vst3',update]:
        run('xcrun','stapler','staple',bundle);run('xcrun','stapler','validate',bundle);verify(bundle)
elif args.phase=='make-dmg':
    accepted('app');run('xcrun','stapler','validate',app)
    stage=folder/'disk-image'
    if stage.exists():shutil.rmtree(stage)
    stage.mkdir();run('ditto',app,stage/app.name)
    (stage/'INSTALL.txt').write_text(f'''Slice Groups {args.version} — Apple Silicon, macOS 14 or later\n\n1. Close Ableton and all other audio hosts.\n2. Open Install Slice Groups.app, confirm your host is closed, and click Install.\n3. Reopen your host and rescan VST3 plug-ins if necessary.\n\nThe installer updates an existing user or writable system installation in place. For new installations it uses ~/Library/Audio/Plug-Ins/VST3/. Previous versions are backed up. Samples, tags and application settings are untouched. If duplicate user and system installations exist, remove the unwanted copy in Finder first.\n\nThis download is Developer ID signed and notarised by Apple. A normal first-open confirmation may appear; no quarantine-removal command is needed.\n\nManual installation: the plug-in is inside Install Slice Groups.app (right-click > Show Package Contents), at Contents/PlugIns/Slice Groups.vst3. Copy it to your VST3 folder while the host is closed.\n\nSource, license and downloads: https://github.com/dcupmusic/slice-groups-releases\n''')
    if dmg.exists():dmg.unlink()
    run('hdiutil','create','-volname',f'Slice Groups {args.version}','-srcfolder',stage,'-format','UDZO',dmg)
    sign(dmg)
elif args.phase=='submit-dmg': submit(dmg,'dmg')
elif args.phase=='status-dmg': status('dmg')
elif args.phase=='finish':
    accepted('app');accepted('dmg');run('xcrun','stapler','staple',dmg);run('xcrun','stapler','validate',dmg)
    run('spctl','--assess','--type','execute','--verbose=2',app)
    run('spctl','--assess','--type','open','--context','context:primary-signature','--verbose=2',dmg)
    manual=folder/'manual-download'
    if manual.exists():shutil.rmtree(manual)
    manual.mkdir();run('ditto',payload,manual/payload.name)
    (manual/'INSTALL.txt').write_text(f"""Slice Groups {args.version} — Apple Silicon, macOS 14 or later\n\nThis is the manual-install ZIP. For the guided installer, download the AppleSilicon.dmg from GitHub instead.\n\n1. Close your audio host.\n2. Replace your existing Slice Groups.vst3 in its current location. For a new installation, use Finder > Go > Go to Folder and enter ~/Library/Audio/Plug-Ins/VST3/ (create it if necessary).\n3. Copy the entire Slice Groups.vst3 bundle there, then restart your host and rescan plug-ins. Avoid installing duplicate copies in both user and system VST3 folders.\n\nThe plugin is Developer ID signed, notarised by Apple, and has its approval ticket attached. No quarantine-removal command should be needed.\n\nSource and downloads: https://github.com/dcupmusic/slice-groups-releases\n""")
    archive(manual,folder/f'SliceGroups-{args.version}-AppleSilicon.zip')
    files=[dmg,folder/f'SliceGroups-{args.version}-AppleSilicon.zip']
    (folder/'SHA256SUMS.txt').write_text(''.join(hashlib.file_digest(file.open('rb'),'sha256').hexdigest()+'  '+file.name+'\n' for file in files))
    print('Ready: signed/notarised DMG installer and stapled manual-install ZIP. No installed plug-in was changed.')
