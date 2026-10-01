import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('package_web', Path(__file__).resolve().parents[1] / 'tools/package_web.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PackageTests(unittest.TestCase):
    def test_pair_and_in_place_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            serial, threaded, output = root / 'serial', root / 'threaded', root / 'site'
            for build, bootstrap in ((serial, 'chibi-default.js'), (threaded, 'chibi-isolation.js')):
                build.mkdir()
                for name in ('chibi.html', 'chibi.js', 'chibi.wasm', 'chibi.data', bootstrap):
                    (build / name).write_text(name)
            (threaded / 'chibi.worker.js').write_text('worker')
            package.copy_build(serial, serial)
            self.assertEqual((serial / 'chibi.html').read_text(), (serial / 'index.html').read_text())
            package.copy_build(serial, output)
            package.copy_build(threaded, output / 'threads', threaded=True)
            for build, bootstrap in ((output, 'chibi-default.js'), (output / 'threads', 'chibi-isolation.js')):
                self.assertTrue((build / bootstrap).is_file())
                self.assertTrue((build / 'index.html').is_file())
                self.assertTrue((build / 'chibi.html').is_file())
            self.assertEqual((output / 'threads/chibi.worker.js').read_text(), 'worker')
            (threaded / 'chibi.wasm').unlink()
            with self.assertRaises(FileNotFoundError):
                package.copy_build(threaded, root / 'broken', threaded=True)
            self.assertFalse((root / 'broken').exists())


if __name__ == '__main__':
    unittest.main()
