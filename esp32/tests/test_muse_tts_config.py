import ctypes
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class TtsConfigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cc = shutil.which('cc')
        if not cc: raise unittest.SkipTest('C compiler required')
        cls.tmp = tempfile.TemporaryDirectory()
        root = Path(cls.tmp.name)
        (root/'esp_err.h').write_text('typedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_INVALID_ARG 1\n#define ESP_ERR_NOT_SUPPORTED 2\n')
        (root/'sdkconfig.h').write_text('#define CONFIG_MUSE_MINIMAX_TTS 1\n#define CONFIG_MUSE_MINIMAX_VOICE "Chinese_huolishaonv"\n')
        (root/'nvs.h').write_text('''#include <stddef.h>
typedef int nvs_handle_t;
#define NVS_READONLY 0
#define NVS_READWRITE 1
int nvs_open(const char *, int, int *);
int nvs_get_str(int, const char *, char *, size_t *);
int nvs_set_str(int, const char *, const char *);
int nvs_commit(int);
void nvs_close(int);
''')
        (root/'fake.c').write_text('''#include <string.h>
#include "nvs.h"
static char saved[256];
static int fail;
void reset(void) { saved[0]=0; fail=0; }
void commit_error(int value) { fail=value; }
int nvs_open(const char *ns, int mode, int *h) { *h=1; return 0; }
int nvs_get_str(int h, const char *key, char *out, size_t *n) {
 if (!saved[0] || strlen(saved)+1>*n) return 3;
 strcpy(out,saved); *n=strlen(saved)+1; return 0;
}
int nvs_set_str(int h, const char *key, const char *v) { strcpy(saved,v); return 0; }
int nvs_commit(int h) { return fail; }
void nvs_close(int h) {}
''')
        source=Path(__file__).resolve().parents[1]/'components/muse'
        lib=root/'tts.so'
        subprocess.run([cc,'-shared','-fPIC','-I',str(root),'-I',str(source),str(source/'muse_tts_config.c'),str(root/'fake.c'),'-o',str(lib)],check=True)
        cls.lib=ctypes.CDLL(str(lib))
        cls.lib.muse_tts_set_voice.argtypes=[ctypes.c_char_p]
        cls.lib.muse_tts_set_voice.restype=ctypes.c_int
        cls.lib.muse_tts_get_voice.argtypes=[ctypes.c_char_p]

    @classmethod
    def tearDownClass(cls): cls.tmp.cleanup()
    def setUp(self): self.lib.reset()
    def get(self):
        out=ctypes.create_string_buffer(129)
        self.lib.muse_tts_get_voice(out)
        return out.value.decode()
    def test_default_and_live_saved_voice(self):
        self.assertEqual(self.get(),'Chinese_huolishaonv')
        voice='Chinese (Mandarin)_Cute_Spirit'
        self.assertEqual(self.lib.muse_tts_set_voice(voice.encode()),0)
        self.assertEqual(self.get(),voice)
        self.assertEqual(self.get(),voice)  # loaded from persistent NVS each time
    def test_invalid_values_preserve_previous_voice(self):
        self.lib.muse_tts_set_voice(b'valid')
        for bad in [None,b'',b'a'*129,b'voice\n',b'"',b'\\',b'\xff']:
            self.assertEqual(self.lib.muse_tts_set_voice(bad),1)
            self.assertEqual(self.get(),'valid')
    def test_maximum_length(self):
        self.assertEqual(self.lib.muse_tts_set_voice(b'x'*128),0)
        self.assertEqual(self.get(),'x'*128)
    def test_commit_failure_is_reported(self):
        self.lib.commit_error(17)
        self.assertEqual(self.lib.muse_tts_set_voice(b'valid'),17)
