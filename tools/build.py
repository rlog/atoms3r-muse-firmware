# SPDX-License-Identifier: Apache-2.0
"""Build or flash ATOMS3R after activating ESP-IDF v6.0.1."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
from configure import ROOT, configure

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--config',type=Path,default=ROOT/'config/local.json')
    ap.add_argument('--action',choices=['build','flash','monitor','menuconfig'],default='build')
    ap.add_argument('--port',help='Serial port for flash or monitor (e.g. COM5 or /dev/ttyACM0)')
    args=ap.parse_args()
    idf=Path(os.environ.get('IDF_PATH',''))/'tools/idf.py'
    if not idf.is_file(): ap.error('Activate ESP-IDF v6.0.1 first (IDF_PATH is missing).')
    if args.action in ('flash','monitor') and not args.port:
        ap.error('--port is required for flash or monitor')
    try:
        configure(args.config)
    except (OSError,ValueError) as error:
        ap.exit(2,'Configuration error: '+str(error)+'\n')
    cmd=[sys.executable,str(idf),'-B','build-muse-m5stack-atoms3r',
         '-DCMAKE_NINJA_FORCE_RESPONSE_FILE=ON','-DIDF_TARGET=esp32s3',
         '-DSDKCONFIG=build-muse-m5stack-atoms3r/sdkconfig',
         '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;devices/sdkconfig.muse;devices/sdkconfig.muse-m5stack-atoms3r;config/sdkconfig.local']
    if args.port: cmd+=['-p',args.port]
    cmd+=['build','flash'] if args.action=='flash' else [args.action]
    return subprocess.call(cmd,cwd=ROOT/'esp32')

if __name__=='__main__': sys.exit(main())
