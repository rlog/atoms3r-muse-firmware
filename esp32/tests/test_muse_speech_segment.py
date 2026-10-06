import ctypes
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SpeechSegmentTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('c++')
        if not compiler:
            raise unittest.SkipTest('C++ compiler required')
        cls.tmp = tempfile.TemporaryDirectory()
        src = Path(cls.tmp.name) / 'segment.cpp'
        include = Path(__file__).resolve().parents[1] / 'components/muse'
        src.write_text('#include "muse_speech_segment.h"\nextern "C" size_t segment(const char *s, bool done) { return muse_speech_segment(s, done); }\nextern "C" size_t skip(const char *s) { return muse_speech_skip_space(s); }\n')
        lib = Path(cls.tmp.name) / 'segment.so'
        subprocess.run([compiler, '-shared', '-fPIC', '-I', str(include), str(src), '-o', str(lib)], check=True)
        cls.lib = ctypes.CDLL(str(lib))
        cls.lib.segment.argtypes = [ctypes.c_char_p, ctypes.c_bool]
        cls.lib.segment.restype = ctypes.c_size_t
        cls.lib.skip.argtypes = [ctypes.c_char_p]
        cls.lib.skip.restype = ctypes.c_size_t

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_reply_arrives_in_deltas(self):
        pending, spoken = '', []
        for delta in ['你好', '，我', '是助手。第', '二句话！尾', '巴']:
            pending += delta
            while (n := self.lib.segment(pending.encode(), False)):
                raw = pending.encode()
                spoken.append(raw[:n].decode())
                pending = raw[n:].decode()
        self.assertEqual(spoken, ['你好，我是助手。', '第二句话！'])
        self.assertEqual(self.lib.segment(pending.encode(), True), len('尾巴'.encode()))
        self.assertEqual(''.join(spoken) + pending, '你好，我是助手。第二句话！尾巴')

    def test_long_sentence_preserves_four_byte_unicode(self):
        raw = ('啊😀' * 70).encode()
        n = self.lib.segment(raw, False)
        self.assertGreaterEqual(n, 240)
        self.assertLessEqual(n, 243)
        self.assertEqual(raw[:n].decode() + raw[n:].decode(), raw.decode())

    def test_partial_character_is_not_emitted(self):
        self.assertEqual(self.lib.segment('你'.encode()[:2], True), 0)
        self.assertEqual(self.lib.segment(b'', True), 0)

    def test_english_sentence_and_final_short_tail(self):
        self.assertEqual(self.lib.segment(b'Hello! Next', False), 6)
        self.assertEqual(self.lib.segment(b'Next', False), 0)
        self.assertEqual(self.lib.segment(b'Next', True), 4)

    def test_blank_lines_are_not_speech_requests(self):
        pending = '\n\n第一段。\r\n \t\n第二段！\n\n\n第三段？\n\n'.encode()
        spoken = []
        while pending:
            pending = pending[self.lib.skip(pending):]
            n = self.lib.segment(pending, True)
            if not n: break
            spoken.append(pending[:n].decode())
            pending = pending[n:]
        self.assertEqual(spoken, ['第一段。', '第二段！', '第三段？'])
        self.assertEqual(pending, b'')

    def test_whitespace_only_and_unicode_spaces(self):
        for text in ['', '\n\r\n\t  ', '\u3000\u00a0\n']:
            raw = text.encode()
            self.assertEqual(self.lib.skip(raw), len(raw))
        raw = '\n\u3000正文'.encode()
        self.assertEqual(raw[self.lib.skip(raw):].decode(), '正文')

    def test_blank_delta_then_more_text(self):
        pending = b''
        spoken = []
        for delta in ['第一段。', '\n', '\n  ', '\r\n', '第二段', '。', '\n\n']:
            pending += delta.encode()
            pending = pending[self.lib.skip(pending):]
            while (n := self.lib.segment(pending, False)):
                spoken.append(pending[:n].decode())
                pending = pending[n:]
                pending = pending[self.lib.skip(pending):]
        self.assertEqual(spoken, ['第一段。', '第二段。'])
        self.assertEqual(pending, b'')
