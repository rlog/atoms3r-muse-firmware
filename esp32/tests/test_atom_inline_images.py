import base64
import ctypes
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
class AtomInlineImages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('cc'): raise unittest.SkipTest('C compiler required')
        cls.tmp=tempfile.TemporaryDirectory()
        src=Path(cls.tmp.name)/'inline.c'
        src.write_text(r'''
#include "control_message.h"
#include "image_base64.h"
#include "muse_chat_priv.h"
#include <stdlib.h>
static control_message_t stream;
static int count,live,fail_alloc;
static size_t last_len;
static uint32_t last_hash;
static void *allocate(size_t n) { if(fail_alloc) return NULL; void *p=malloc(n); if(p) ++live; return p; }
static void release(void *p) { if(p) --live; free(p); }
static void ready(void *ctx,const uint8_t *p,size_t n) {
 (void)ctx; ++count; last_len=n-4; last_hash=2166136261U;
 for(size_t i=4;i<n;++i) last_hash=(last_hash^p[i])*16777619U;
}
void reset(void) { control_message_reset(&stream,release); count=0; fail_alloc=0; last_len=0; }
int feed(const void *p,size_t n) { return control_message_feed(&stream,p,n,allocate,release,ready,NULL); }
int frames(void) { return count; }
int allocations(void) { return live; }
void fail(int on) { fail_alloc=on; }
size_t length(void) { return last_len; }
uint32_t hash(void) { return last_hash; }
int decode(const char *text,void *out,size_t cap,size_t *n) { return image_base64_decode(text,out,cap,n); }
const char *policy(void) { return MUSE_ATOM_IMAGE_POLICY; }
const char *note(void) { return MUSE_HATCH_NOTE_HEAD MUSE_HATCH_NOTE_TAIL; }
''')
        lib=Path(cls.tmp.name)/'inline.so'
        subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror',
                        '-DCONFIG_MUSE_BOARD_M5STACK_ATOMS3R=1','-DCONFIG_MUSE_REPLY_IMAGES=1',
                        '-I',str(ROOT/'main'),'-I',str(ROOT/'components/muse'),
                        str(src),str(ROOT/'main/image_base64.c'),'-lmbedcrypto','-o',str(lib)],check=True)
        cls.lib=ctypes.CDLL(str(lib))
        cls.lib.feed.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
        cls.lib.decode.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p]
        cls.lib.policy.restype=ctypes.c_char_p
        cls.lib.note.restype=ctypes.c_char_p
        cls.lib.length.restype=ctypes.c_size_t
        cls.lib.hash.restype=ctypes.c_uint32
        cpp=Path(cls.tmp.name)/'portable.cpp'
        cpp.write_text('#include "control_message.h"\n')
        subprocess.run(['c++','-fsyntax-only','-Wall','-Wextra','-Werror','-I',str(ROOT/'main'),str(cpp)],check=True)
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def setUp(self): self.lib.reset()
    def tearDown(self):
        self.lib.reset()
        self.assertEqual(self.lib.allocations(),0)
    def feed(self,data): return self.lib.feed(data,len(data))
    def decode(self,text,cap=36864):
        out=ctypes.create_string_buffer(cap); n=ctypes.c_size_t()
        ok=self.lib.decode(text,out,cap,ctypes.byref(n))
        return ok,out.raw[:n.value]
    def test_large_control_json_fragmented_at_every_boundary(self):
        content=json.dumps({'method':'link.invoke','command':'display.draw_base64',
                            'params':{'data_base64':base64.b64encode(bytes(range(256))*100).decode()}}).encode()
        frame=struct.pack('<I',len(content))+content
        expected=2166136261
        for c in content: expected=((expected^c)*16777619)&0xffffffff
        for chunk in [1,3,4,17,8192,12288,65536]:
            self.lib.reset()
            for i in range(0,len(frame),chunk): self.assertTrue(self.feed(frame[i:i+chunk]))
            self.assertEqual(self.lib.frames(),1)
            self.assertEqual(self.lib.length(),len(content))
            self.assertEqual(self.lib.hash(),expected)
    def test_multiple_messages_and_incomplete_message(self):
        a=struct.pack('<I',3)+b'abc'; b=struct.pack('<I',4)+b'defg'
        self.assertTrue(self.feed(a+b[:6])); self.assertEqual(self.lib.frames(),1)
        self.assertTrue(self.feed(b[6:])); self.assertEqual(self.lib.frames(),2)
        self.assertEqual(self.lib.allocations(),0)
    def test_oversize_and_allocation_failure_fail_closed(self):
        self.assertFalse(self.feed(struct.pack('<I',65537)))
        self.assertFalse(self.feed(b'abcdef'))
        self.lib.reset(); self.lib.fail(1)
        self.assertFalse(self.feed(struct.pack('<I',12)))
        self.assertFalse(self.feed(b'x'*12))
    def test_base64_roundtrip_and_maximum(self):
        for raw in [b'a',b'ab',b'abc',bytes(range(256))*144]:
            self.assertEqual(self.decode(base64.b64encode(raw)),(1,raw))
    def test_base64_rejects_malformed_and_limits(self):
        for text in [b'',b'abc',b'ab=c',b'====',b'a===',b'YWJj\n',b'YW J',
                     b'data:image/png;base64,YWJj',b'AAAA'*12289]:
            self.assertEqual(self.decode(text)[0],0)
        self.assertEqual(self.decode(b'YWJj',2)[0],0)
    def test_voice_and_text_policy_matches_atom_scope(self):
        note=json.loads(self.lib.note().decode())
        policy=self.lib.policy().decode()
        self.assertEqual(note['message'],policy)
        self.assertIn('128x128',policy)
        self.assertIn('display.draw_base64',policy)
        self.assertIn('Do not upload',policy)
        self.assertEqual(note['items'][0]['data_base64'],'')
        session=(ROOT/'components/muse/muse_chat_session.cpp').read_text()
        self.assertIn('snprintf(request,n,"%s%s",MUSE_ATOM_IMAGE_POLICY,text)',session)
    def test_command_is_advertised_and_dispatched(self):
        control=(ROOT/'main/noise_control.cpp').read_text()
        app=(ROOT/'main/app.c').read_text()
        self.assertIn('add_command(commands,"display.draw_base64"',control)
        self.assertIn('strcmp(command,"display.draw_base64")==0',app)
        self.assertIn('image_fetch_base64_start(data,draw_url_done,ctx',app)
