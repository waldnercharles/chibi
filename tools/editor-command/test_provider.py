import os
import unittest
from unittest.mock import patch
from provider import settings


class LocalProviderTests(unittest.TestCase):
    def test_cloud_routes_are_rejected(self):
        for url in ['https://example.com/v1', 'http://127.0.0.1.evil.test/v1', 'http://user:secret@localhost/v1']:
            with self.subTest(url=url), patch.dict(os.environ, {'CHIBI_MODEL_URL': url}):
                with self.assertRaises(ValueError):
                    settings()
        with patch.dict(os.environ, {'CHIBI_MODEL_URL': 'http://127.0.0.1:11435/v1', 'CHIBI_MODEL': 'qwen3.5:cloud'}):
            with self.assertRaises(ValueError):
                settings()

    def test_loopback_configuration(self):
        with patch.dict(os.environ, {'CHIBI_MODEL_URL': 'http://127.0.0.1:11435/v1', 'CHIBI_MODEL': 'qwen3.5:4b'}):
            self.assertEqual(settings(), ('http://127.0.0.1:11435/v1', 'qwen3.5:4b'))


if __name__ == '__main__':
    unittest.main()
