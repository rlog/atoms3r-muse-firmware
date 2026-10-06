# SPDX-License-Identifier: Apache-2.0
"""Validate private JSON and generate ignored ESP-IDF configuration."""
import argparse
import base64
import ipaddress
import json
from pathlib import Path
import re
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'esp32/build-muse-m5stack-atoms3r'

def string(value, name, limit=1024):
    if not isinstance(value, str) or len(value) > limit or any(ord(c)<32 or ord(c)>126 for c in value):
        raise ValueError(name + ' must be a bounded printable ASCII string')
    return value

def settings(document):
    if not isinstance(document, dict) or set(document) != {'muse','minimax','proxy'}:
        raise ValueError('configuration must contain muse, minimax and proxy sections')
    sections = {'muse':{'sdk_token'}, 'minimax':{'enabled','api_key','url','model','voice'},
                'proxy':{'enabled','method','server','port','key'}}
    for section, keys in sections.items():
        if not isinstance(document[section], dict) or set(document[section]) != keys:
            raise ValueError(section + ' has missing or unknown configuration keys')
    muse, speech, proxy = (document[k] for k in sections)
    for section in (speech,proxy):
        if type(section['enabled']) is not bool:
            raise ValueError('enabled must be true or false')
    token=string(muse['sdk_token'],'muse.sdk_token')
    api_key=string(speech['api_key'],'minimax.api_key')
    url=string(speech['url'],'minimax.url')
    parsed=urlsplit(url)
    if parsed.scheme!='https' or not parsed.hostname or parsed.username or parsed.password or parsed.fragment:
        raise ValueError('minimax.url must be an HTTPS endpoint without user info or fragment')
    model=string(speech['model'],'minimax.model',128)
    voice=string(speech['voice'],'minimax.voice',128)
    if not model or not voice or '"' in voice or '\\' in voice:
        raise ValueError('model and voice must be nonempty; voice must not contain quotes or backslashes')
    if speech['enabled'] and not api_key:
        raise ValueError('minimax.api_key is required when MiniMax is enabled')
    if proxy['method']!='2022-blake3-aes-128-gcm':
        raise ValueError('proxy.method must be 2022-blake3-aes-128-gcm')
    server=string(proxy['server'],'proxy.server',64)
    key=string(proxy['key'],'proxy.key',128)
    port=proxy['port']
    if type(port) is not int or not 1<=port<=65535:
        raise ValueError('proxy.port must be an integer from 1 to 65535')
    if proxy['enabled']:
        try:
            ipaddress.IPv4Address(server)
            decoded=base64.b64decode(key,validate=True)
        except (ValueError,TypeError):
            raise ValueError('proxy requires a valid IPv4 server and base64 key') from None
        if len(decoded)!=16 or base64.b64encode(decoded).decode()!=key:
            raise ValueError('proxy.key must be canonical base64 for a 16-byte PSK')
    return {'GADGET_SDK_TOKEN':token,
            'MUSE_MINIMAX_TTS':speech['enabled'], 'MUSE_MINIMAX_API_KEY':api_key if speech['enabled'] else '',
            'MUSE_MINIMAX_URL':url, 'MUSE_MINIMAX_MODEL':model, 'MUSE_MINIMAX_VOICE':voice,
            'MUSE_SS2022_PROXY':proxy['enabled'], 'MUSE_SS2022_SERVER':server if proxy['enabled'] else '',
            'MUSE_SS2022_PORT':port, 'MUSE_SS2022_KEY':key if proxy['enabled'] else '',
            'LWIP_MAX_SOCKETS':24, 'LWIP_LOOPBACK_MAX_PBUFS':32, 'LWIP_SNTP_MAX_SERVERS':2}

def render(values):
    lines=[]
    for name,value in values.items():
        if type(value) is bool:
            lines.append('CONFIG_'+name+'=y' if value else '# CONFIG_'+name+' is not set')
        else:
            lines.append('CONFIG_'+name+'='+json.dumps(value,ensure_ascii=False))
    return '\n'.join(lines)+'\n'

def merge(existing,values):
    # Updating existing sdkconfig is necessary: ESP-IDF defaults do not override it.
    kept=[]
    for line in existing.splitlines():
        match=re.match(r'(?:# )?CONFIG_([A-Z0-9_]+)(?:=| is not set)',line)
        if not match or match[1] not in values:
            kept.append(line)
    return '\n'.join(kept)+'\n'+render(values)

def private_write(path,text):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(text,encoding='utf-8',newline='\n')
    path.chmod(0o600)

def configure(config_path, root=ROOT):
    values=settings(json.loads(Path(config_path).read_text(encoding='utf-8-sig')))
    private_write(root/'esp32/config/sdkconfig.local',render(values))
    config=root/'esp32/build-muse-m5stack-atoms3r/sdkconfig'
    if config.exists():
        private_write(config,merge(config.read_text(encoding='utf-8'),values))
    if not values['GADGET_SDK_TOKEN']:
        print('SDK token is empty: builds work, but Muse pairing requires your token.')
    print('ATOMS3R build configuration updated; credential values are hidden.')
    return values

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--config',type=Path,default=ROOT/'config/local.json')
    args=ap.parse_args()
    try:
        configure(args.config)
    except (OSError,ValueError) as error:
        # Parser errors report locations; validators never echo credential values.
        ap.exit(2,'Configuration error: '+str(error)+'\n')

if __name__=='__main__': main()
