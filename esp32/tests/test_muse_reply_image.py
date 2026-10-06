import ctypes
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

class ReplyImages(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not shutil.which('cc'):
            raise unittest.SkipTest('C compiler required')
        for dep in ['libpng', 'libcjson']:
            if subprocess.run(['pkg-config','--exists',dep]).returncode:
                raise unittest.SkipTest('libpng-dev and libcjson-dev required')
        cls.tmp=tempfile.TemporaryDirectory()
        root=Path(__file__).resolve().parents[1]
        src=Path(cls.tmp.name)/'wrapper.c'
        src.write_text("""
#include "image_png.h"
#include "muse_reply_image.h"
#include <stdlib.h>
typedef struct { const unsigned char *data; size_t len,pos; } input_t;
static bool read_bytes(void *user,void *out,size_t n) {
 input_t *r=user; if(n>r->len-r->pos) return false;
 memcpy(out,r->data+r->pos,n); r->pos+=n; return true;
}
int decode(const void *data,size_t len,uint16_t *out,int *w,int *h) {
 input_t r={data,len,0}; uint16_t *p=NULL;
 if(!image_png_decode(read_bytes,&r,malloc,free,128,128,&p,w,h)) return 0;
 memcpy(out,p,(*w)*(*h)*2); free(p); return 1;
}
int extract(const char *data,char *out,size_t cap) {
 cJSON *j=cJSON_Parse(data); bool ok=muse_reply_image(j,out,cap); cJSON_Delete(j); return ok;
}
""")
        flags=subprocess.check_output(['pkg-config','--cflags','--libs','libpng','libcjson'],text=True).split()
        lib=Path(cls.tmp.name)/'images.so'
        subprocess.run(['cc','-shared','-fPIC','-Wall','-Wextra','-Werror','-I',str(root/'main'),
                        '-I',str(root/'components/muse'),str(src),str(root/'main/image_png.c'),
                        '-o',str(lib),*flags],check=True)
        cls.lib=ctypes.CDLL(str(lib))
        cls.lib.decode.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_void_p,ctypes.c_void_p]
        cls.lib.extract.argtypes=[ctypes.c_char_p,ctypes.c_void_p,ctypes.c_size_t]
    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def extract(self,obj,cap=2048):
        out=ctypes.create_string_buffer(cap)
        ok=self.lib.extract(json.dumps(obj).encode(),out,cap)
        return out.value.decode() if ok else None
    def test_schema_and_markdown(self):
        url='https://images.example/a?signature=abc'
        for obj in [
            {'attachments':[{'mime_type':'image/png','url':url}]},
            {'content':[{'type':'image_url','image_url':{'url':url}}]},
            {'images':[url]},
            {'data':{'images':[{'mime':'image/png','path':url,'variants':{'original':url}}]}},
            {'display_text':f'这是图片：![图]({url})'},
            {'content':f'![图](<{url}> "标题")'},
        ]:
            with self.subTest(obj=obj): self.assertEqual(self.extract(obj),url)
    def test_reject_unrelated_and_unsafe(self):
        for obj in [{'url':'https://example/a'},{'images':['http://example/a']},
                    {'images':['https://example/a\nInjected']},{'images':['data:image/png;base64,aaaa']}]:
            self.assertIsNone(self.extract(obj))
        self.assertIsNone(self.extract({'images':['https://example/'+'x'*100]},32))
    @staticmethod
    def png(w,h,pixel,interlace=False):
        def chunk(tag,data):
            return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data))
        raw=b''
        passes=[(0,0,1,1)] if not interlace else [(0,0,8,8),(4,0,8,8),(0,4,4,8),(2,0,4,4),(0,2,2,4),(1,0,2,2),(0,1,1,2)]
        for x0,y0,dx,dy in passes:
            if x0>=w: continue
            for y in range(y0,h,dy):
                raw+=b'\0'+b''.join(bytes(pixel(x,y)) for x in range(x0,w,dx))
        return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,int(interlace)))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
    def decode(self,data):
        out=(ctypes.c_uint16*(128*128))(); w=ctypes.c_int(); h=ctypes.c_int()
        ok=self.lib.decode(data,len(data),out,ctypes.byref(w),ctypes.byref(h))
        return ok,w.value,h.value,bytes(out)
    def test_large_portrait_and_alpha(self):
        data=self.png(1024,1536,lambda x,y:(255,0,0,255))
        ok,w,h,raw=self.decode(data)
        self.assertEqual((ok,w,h),(1,85,128))
        self.assertEqual(raw[:2],b'\xf8\x00')
        ok,w,h,raw=self.decode(self.png(2,1,lambda x,y:(0,0,0,0 if x==0 else 255)))
        self.assertEqual((ok,w,h,raw[:4]),(1,2,1,b'\xff\xff\x00\x00'))
    def test_adam7_matches_regular(self):
        pixel=lambda x,y:(x%256,y%256,(x+y)%256,255)
        self.assertEqual(self.decode(self.png(301,517,pixel,True)),
                         self.decode(self.png(301,517,pixel)))
    def test_corrupt_truncated_and_oversized(self):
        valid=self.png(8,8,lambda x,y:(0,255,0,255))
        corrupt=bytearray(valid); corrupt[-5]^=1
        for data in [valid[:-12],bytes(corrupt),b'not an image',
                     self.png(4097,1,lambda x,y:(0,0,0,255))]:
            self.assertEqual(self.decode(data)[0],0)
