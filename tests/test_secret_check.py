# SPDX-License-Identifier: Apache-2.0
import contextlib, importlib.util, io, subprocess, tempfile, unittest
from pathlib import Path
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('check_secrets',ROOT/'tools/check_secrets.py')
module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)

class SecretCheck(unittest.TestCase):
    def git(self,root,*args):
        subprocess.run(['git','-C',str(root),*args],check=True,capture_output=True)

    def scan(self,root):
        output=io.StringIO()
        with patch.object(module,'ROOT',root), contextlib.redirect_stderr(output), contextlib.redirect_stdout(output):
            result=module.main()
        return result,output.getvalue()

    def test_scans_staged_bytes_even_if_working_file_is_clean(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); self.git(root,'init')
            secret='sk-api-'+'synthetic-test-value'*3
            file=root/'source.txt'; file.write_text(secret)
            self.git(root,'add','source.txt'); file.write_text('clean working file')
            result,output=self.scan(root)
            self.assertEqual(result,1)
            self.assertIn('source.txt',output)
            self.assertNotIn(secret,output)

    def test_blocks_binary_artifacts_and_allows_only_named_public_fixture(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); self.git(root,'init')
            file=root/'esp32/tests/link_pairing_handshake_harness.c'; file.parent.mkdir(parents=True)
            file.write_text('mgst_'+'A'*43)
            self.git(root,'add','.'); self.assertEqual(self.scan(root)[0],0)
            (root/'firmware.bin').write_bytes(b'not-for-publication')
            self.git(root,'add','.'); self.assertEqual(self.scan(root)[0],1)

if __name__=='__main__': unittest.main()
