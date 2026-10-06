# SPDX-License-Identifier: Apache-2.0
"""Check staged/tracked source files without printing secret contents."""
import json
import io
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
PATTERNS=[re.compile(rb'mgst_[A-Za-z0-9_-]{20,}'),
          re.compile(rb'sk-api-[A-Za-z0-9_-]{20,}'),
          re.compile(rb'(?:gh[pousr]_|github_pat_)[A-Za-z0-9_]{20,}'),
          re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')]

def main():
    entries=[]
    for entry in subprocess.check_output(['git','ls-files','-s','-z'],cwd=ROOT).decode().split('\0'):
        if entry:
            metadata,name=entry.split('\t',1)
            entries.append((name,metadata.split()[1]))
    # Scan the exact index blobs that will be committed, including staged edits.
    batch=subprocess.check_output(['git','cat-file','--batch'],
        input=''.join(oid+'\n' for _,oid in entries).encode(),cwd=ROOT)
    blobs=io.BytesIO(batch)
    private=ROOT/'config/local.json'; known=[]
    if private.exists():
        config=json.loads(private.read_text(encoding='utf-8-sig'))
        for section,key in [('muse','sdk_token'),('minimax','api_key'),('proxy','key')]:
            value=config.get(section,{}).get(key,'')
            if len(value)>=8: known.append(value.encode())
    failures=[]
    for name,_ in entries:
        path=ROOT/name
        header=blobs.readline().split()
        if len(header)!=3 or header[1]!=b'blob':
            raise RuntimeError('unexpected Git object in source index')
        data=blobs.read(int(header[2])); blobs.read(1)
        forbidden=(name.startswith(('config/local.','esp32/config/sdkconfig.local')) or
                   '/build-' in name or '/managed_components/' in name or
                   path.suffix.lower() in ('.bin','.elf','.pem','.key'))
        # Upstream's all-zero synthetic SDK token is an intentional public vector.
        fixture=b'mgst_'+b'A'*43
        matches=[m.group() for p in PATTERNS for m in p.finditer(data)]
        if name=='esp32/tests/link_pairing_handshake_harness.c':
            matches=[m for m in matches if m!=fixture]
        if forbidden or matches or any(s in data for s in known):
            failures.append(name)
    if failures:
        print('Secret or private artifact detected in: '+', '.join(failures),file=sys.stderr)
        return 1
    print('Source secret/artifact check passed ('+str(len(entries))+' files).')
    return 0

if __name__=='__main__': sys.exit(main())
