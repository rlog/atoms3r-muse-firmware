import ctypes as C,json,unittest,os,tempfile,subprocess,shutil,struct
from pathlib import Path
try:
 from cryptography.hazmat.primitives.ciphers.aead import AESGCM
except ImportError:
 raise unittest.SkipTest('cryptography required for independent SS2022 reference vectors')
cc=shutil.which('cc')
if not cc or os.name=='nt':raise unittest.SkipTest('POSIX compiler and libmbedcrypto required')
_tmp=tempfile.TemporaryDirectory()
_src=Path(__file__).resolve().parents[1]/'components/muse'
_lib=Path(_tmp.name)/'ss2022.so'
subprocess.run([cc,'-shared','-fPIC','-O2','-std=c11','-Wall','-Wextra','-Werror','-DBLAKE3_NO_SSE2','-DBLAKE3_NO_SSE41','-DBLAKE3_NO_AVX2','-DBLAKE3_NO_AVX512','-DBLAKE3_USE_NEON=0',str(_src/'muse_ss2022.c'),*[str(_src/'third_party/blake3'/n) for n in ['blake3.c','blake3_dispatch.c','blake3_portable.c']],'-lmbedcrypto','-o',str(_lib)],check=True)
L=C.CDLL(str(_lib))
P=C.c_void_p;Z=C.c_size_t;U=C.c_uint64;B=C.c_bool
EMIT=C.CFUNCTYPE(B,P,P,Z)
for name,args,ret in [('ss2022_cipher_init',[P,P,P],B),('ss2022_cipher_free',[P],None),('ss2022_request',[P,P,C.c_char_p,C.c_uint16,U,P,Z,P,Z,P],B),('ss2022_payload',[P,P,Z,P,Z,P],B),('ss2022_rx_init',[P,P,P],None),('ss2022_rx_free',[P],None),('ss2022_feed',[P,P,Z,U,EMIT,P],B)]:
 f=getattr(L,name);f.argtypes=args;f.restype=ret
V={'psk': '000102030405060708090a0b0c0d0e0f', 'salt': '202122232425262728292a2b2c2d2e2f', 'padding': '000102030405060708090a0b0c0d0e0f10', 'now': 1800000000, 'request': '202122232425262728292a2b2c2d2e2fced439ccd5c5422332977ea81b33fa7a4b0496c50a01dd8f1c03281dd6f39803cc31ff0985a975cdb49c4db623129df1c96520299f5e28cc0c3f12ee50cfe3b23a198e749ad8f027847633a256', 'payload': '68656c6c6f2066726f6d207265616c2070726f64756374696f6e20636f6465', 'packet': 'bd3724054eb6e79f2b1823c60fc060f923c50b4933bb9ab1fea7e8d698965190eb31bb9cedb745d0f94330bc08e4af6f1122868ceb0104061ed63dac30662f06a3'}
# Public fixture key, independently derived using upstream Python BLAKE3.
_server_key=bytes.fromhex('722b3033c5d021365a8521bfb41157a3')
def _response(stamp,request_salt,large=False):
 def seal(n,data):return AESGCM(_server_key).encrypt(n.to_bytes(12,'little'),data,None)
 body=b'server first bytes'
 wire=bytes(range(128,144))+seal(0,b'\x01'+struct.pack('!Q',stamp)+request_salt+struct.pack('!H',len(body)))+seal(1,body)
 bodies=[b'second chunk']+([bytes(range(256))*255+b'x'*255] if large else [])
 n=2
 for b in bodies:wire+=seal(n,struct.pack('!H',len(b)))+seal(n+1,b);n+=2
 return wire,body+b''.join(bodies)
_salt=bytes.fromhex(V['salt'])
_w,_p=_response(V['now'],_salt,True)
V.update(response=_w.hex(),plain=_p.hex(),bad_time=_response(V['now']-31,_salt)[0].hex(),bad_salt=_response(V['now'],b'X'*16)[0].hex())

def hx(n):return bytes.fromhex(V[n])
class Rx:
 def __init__(self,key=None,salt=None):
  self.mem=C.create_string_buffer(140000);self.data=bytearray();self.emit=EMIT(lambda _,p,n:self.take(p,n));L.ss2022_rx_init(self.mem,key or hx('psk'),salt or hx('salt'))
 def take(self,p,n):self.data+=C.string_at(p,n);return True
 def feed(self,p,now=None):return L.ss2022_feed(self.mem,p,len(p),V['now'] if now is None else now,self.emit,None)
 def close(self):L.ss2022_rx_free(self.mem)
class ProtocolTests(unittest.TestCase):
 def test_request_and_payload_match_independent_reference(self):
  c=C.create_string_buffer(64);self.assertTrue(L.ss2022_cipher_init(c,hx('psk'),hx('salt')))
  out=C.create_string_buffer(4096);n=Z()
  self.assertTrue(L.ss2022_request(c,hx('salt'),b'api.muse.ai',443,V['now'],hx('padding'),len(hx('padding')),out,len(out),C.byref(n)))
  self.assertEqual(out.raw[:n.value],hx('request'))
  data=hx('payload');self.assertTrue(L.ss2022_payload(c,data,len(data),out,len(out),C.byref(n)));self.assertEqual(out.raw[:n.value],hx('packet'));L.ss2022_cipher_free(c)
 def test_split_reads_and_maximum_chunk(self):
  for width in [1,7,59,2048,65551,140000]:
   r=Rx();wire=hx('response')
   for i in range(0,len(wire),width):self.assertTrue(r.feed(wire[i:i+width]))
   self.assertEqual(bytes(r.data),hx('plain'));r.close()
 def test_bad_timestamp_rejected(self):
  r=Rx();self.assertFalse(r.feed(hx('bad_time')));self.assertEqual(r.data,b'');r.close()
 def test_request_salt_binding(self):
  r=Rx();self.assertFalse(r.feed(hx('bad_salt')));self.assertEqual(r.data,b'');r.close()
 def test_bad_header_tag_and_failure_latches(self):
  wire=bytearray(hx('response'));wire[50]^=1;r=Rx();self.assertFalse(r.feed(bytes(wire)));self.assertFalse(r.feed(hx('response')));self.assertEqual(r.data,b'');r.close()
 def test_bad_payload_tag_does_not_emit_plaintext(self):
  wire=bytearray(hx('response'));wire[65]^=1;r=Rx();self.assertFalse(r.feed(bytes(wire)));self.assertEqual(r.data,b'');r.close()
 def test_wrong_key_rejected(self):
  r=Rx(key=b'X'*16);self.assertFalse(r.feed(hx('response')));self.assertEqual(r.data,b'');r.close()
 def test_small_output_buffer(self):
  c=C.create_string_buffer(64);self.assertTrue(L.ss2022_cipher_init(c,hx('psk'),hx('salt')));out=C.create_string_buffer(16);n=Z();self.assertFalse(L.ss2022_payload(c,b'abc',3,out,16,C.byref(n)));L.ss2022_cipher_free(c)
if __name__=='__main__':unittest.main(verbosity=2)
