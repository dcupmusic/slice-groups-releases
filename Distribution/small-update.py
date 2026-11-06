#!/usr/bin/env python3
"""Build a small, offline update payload from complete signed release bundles.
Large immutable assets are reused only after content verification; Mach-O signature
changes are repaired using the new signed prefix and signature, not a signing key.
"""
import argparse, hashlib, json, os, plistlib, stat, struct, subprocess
from pathlib import Path

def sha(data):return hashlib.sha256(data).hexdigest()
def macho_signature(data):
    if data[:4]!=b'\xcf\xfa\xed\xfe':return None
    offset=32
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        command,size=struct.unpack_from('<II',data,offset)
        if size<8 or offset+size>len(data):raise ValueError('Invalid Mach-O command')
        if command==0x1d:return struct.unpack_from('<II',data,offset+8)
        offset+=size
    return None

def build(bundles,output,version):
    blobs=[];indices={};offset=0
    def parts(data):
        nonlocal offset
        result=[]
        for n in range(0,len(data),65536):
            chunk=data[n:n+65536];digest=sha(chunk)
            if digest not in indices:indices[digest]=(offset,len(chunk));blobs.append(chunk);offset+=len(chunk)
            start,length=indices[digest];result.append({'offset':start,'size':length,'sha256':digest})
        return result
    manifest={'format':1,'version':version,'team':'Q5YNM8U8JB','bundles':[]}
    for role,bundle in bundles:
        info=plistlib.loads((bundle/'Contents/Info.plist').read_bytes())
        assert info['CFBundleShortVersionString']==version and info['CFBundleIdentifier']=='audio.slicegroups.plugin'
        subprocess.run(['codesign','--verify','--deep','--strict',str(bundle)],check=True)
        files=[]
        for file in sorted(bundle.rglob('*')):
            if file.is_symlink():raise ValueError('Unexpected bundle symlink: '+str(file))
            if not file.is_file() or file.name=='.DS_Store' or file.name.startswith('._'):continue
            path=str(file.relative_to(bundle));data=file.read_bytes();entry={'path':path,'size':len(data),'sha256':sha(data),'mode':stat.S_IMODE(file.stat().st_mode)}
            if len(data)>=8*1024*1024 and not path.startswith('Contents/MacOS/'):
                signature=macho_signature(data)
                if signature and path.startswith('Contents/Frameworks/'):
                    end,sigsize=signature;assert end+sigsize<=len(data);prefix=min(1024*1024,end)
                    entry.update(strategy='macho',prefix=prefix,bodyEnd=end,bodySha256=sha(data[prefix:end]),prefixParts=parts(data[:prefix]),tailParts=parts(data[end:]))
                else:entry['strategy']='reuse'
            else:entry.update(strategy='payload',parts=parts(data))
            files.append(entry)
        manifest['bundles'].append({'role':role,'name':bundle.name,'files':files})
    header=json.dumps(manifest,separators=(',',':'),sort_keys=True).encode();output.parent.mkdir(parents=True,exist_ok=True)
    with output.open('wb') as target:
        target.write(b'SGUPD001');target.write(struct.pack('<I',len(header)));target.write(header)
        for chunk in blobs:target.write(chunk)
    output.with_suffix('.manifest.json').write_text(json.dumps(manifest,indent=2))
    print('Payload:',output.stat().st_size,'bytes; reusable bytes:',sum(f['size'] for b in manifest['bundles'] for f in b['files'] if f['strategy']!='payload'))
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--plugin',type=Path,required=True);p.add_argument('--standalone',type=Path,required=True);p.add_argument('--version',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();build([('plugin',a.plugin),('standalone',a.standalone)],a.output,a.version)
