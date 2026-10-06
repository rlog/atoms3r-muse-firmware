# SPDX-License-Identifier: Apache-2.0
import contextlib, copy, importlib.util, io, json, tempfile, unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('configure',ROOT/'tools/configure.py')
module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)

class PrivateConfig(unittest.TestCase):
    def setUp(self):
        self.config=json.loads((ROOT/'config/ci.json').read_text())

    def test_updates_existing_build_without_logging_credentials(self):
        self.config['muse']['sdk_token']='unit-test-secret-token'
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); source=root/'local.json'; source.write_text(json.dumps(self.config))
            sdk=root/'esp32/build-muse-m5stack-atoms3r/sdkconfig'; sdk.parent.mkdir(parents=True)
            sdk.write_text('CONFIG_OTHER=y\nCONFIG_GADGET_SDK_TOKEN="old"\n# CONFIG_MUSE_MINIMAX_TTS is not set\n')
            log=io.StringIO()
            with contextlib.redirect_stdout(log): module.configure(source,root)
            text=sdk.read_text()
            self.assertIn('CONFIG_OTHER=y',text)
            self.assertIn('CONFIG_MUSE_MINIMAX_TTS=y',text)
            self.assertNotIn('"old"',text)
            self.assertEqual(text.count('CONFIG_GADGET_SDK_TOKEN='),1)
            self.assertNotIn('unit-test-secret-token',log.getvalue())

    def test_disabled_features_remove_old_secrets(self):
        self.config['minimax']['enabled']=False; self.config['proxy']['enabled']=False
        values=module.settings(self.config)
        self.assertEqual(values['MUSE_MINIMAX_API_KEY'],'')
        self.assertEqual(values['MUSE_SS2022_KEY'],'')
        self.assertIn('# CONFIG_MUSE_SS2022_PROXY is not set',module.render(values))

    def test_rejects_invalid_or_incomplete_config(self):
        changes=[('proxy','port',True),('proxy','port',65536),('proxy','method','vless'),
                 ('proxy','server','example.com'),('proxy','key','invalid'),
                 ('minimax','api_key',''),('minimax','enabled','yes'),
                 ('minimax','voice','bad"voice'),('minimax','url','http://example.com'),
                 ('muse','sdk_token','hidden\nCONFIG_EVIL=y')]
        for section,key,value in changes:
            with self.subTest(section=section,key=key):
                cfg=copy.deepcopy(self.config); cfg[section][key]=value
                with self.assertRaises(ValueError): module.settings(cfg)
        del self.config['proxy']['key']
        with self.assertRaises(ValueError): module.settings(self.config)

if __name__=='__main__': unittest.main()
