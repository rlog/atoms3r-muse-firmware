# SPDX-License-Identifier: Apache-2.0
import importlib.util, sys, tempfile, unittest
from pathlib import Path
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
spec=importlib.util.spec_from_file_location('build',ROOT/'tools/build.py')
module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)

class SigningKey(unittest.TestCase):
    def test_existing_key_is_not_regenerated(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); key=root/'esp32/dev_signing_key.pem'; key.parent.mkdir()
            key.write_text('existing-key')
            with patch.object(module.subprocess,'run') as run:
                self.assertEqual(module.ensure_signing_key(root),0)
                run.assert_not_called()
            self.assertEqual(key.read_text(),'existing-key')

    def test_generates_missing_key_with_explicit_scheme(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); (root/'esp32').mkdir()
            def generate(command,cwd):
                self.assertEqual(command[1:4],['-m','espsecure','generate-signing-key'])
                self.assertIn('rsa3072',command)
                self.assertEqual(cwd,root/'esp32')
                (cwd/'dev_signing_key.pem').write_text('new-key')
                return type('Result',(),{'returncode':0})()
            with patch.object(module.subprocess,'run',side_effect=generate):
                self.assertEqual(module.ensure_signing_key(root),0)

if __name__=='__main__': unittest.main()
