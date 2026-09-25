"""Local workspace saves must survive reload without escaping the project."""
import importlib.util
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path
import socket
import tempfile
import threading
import unittest

spec = importlib.util.spec_from_file_location(
    'web_serve', Path(__file__).resolve().parents[1] / 'tools/web_serve.py')
server = importlib.util.module_from_spec(spec)
spec.loader.exec_module(server)


class WorkspaceSaves(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.project = Path(self.directory.name).resolve()
        self.content = self.project / 'content'
        self.content.mkdir()

    def save(self, path, text='name: Bloom\n'):
        server.save_asset(self.project, self.content, {'path': path, 'text': text})

    def test_save_and_reopen(self):
        self.save('/content/scenes/Bloom.yaml')
        path = self.content / 'scenes/Bloom.yaml'
        self.assertEqual(path.read_text(), 'name: Bloom\n')
        self.save('content/scenes/Bloom.yaml', 'name: Revised\n')
        self.assertEqual(path.read_text(), 'name: Revised\n')

    def test_invalid_document_preserves_previous_asset(self):
        self.save('content/Bloom.yaml')
        with self.assertRaises(ValueError):
            self.save('content/Bloom.yaml', '{')
        self.assertEqual((self.content / 'Bloom.yaml').read_text(), 'name: Bloom\n')

    def test_paths_stay_inside_content(self):
        (self.content / 'escape').symlink_to(self.project, target_is_directory=True)
        for path in ('content/../outside.yaml', '../outside.yaml',
                     'content/escape/outside.yaml', 'content/script.js'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                self.save(path)
        self.assertFalse((self.project / 'outside.yaml').exists())


class ModelProxy(unittest.TestCase):
    def test_forwards_only_to_loopback_http(self):
        for url in ('https://127.0.0.1:11435/api/chat', 'http://example.com/api/chat',
                    'http://user:secret@127.0.0.1:11435/api/chat', 'file:///etc/passwd'):
            with self.subTest(url=url), self.assertRaises(ValueError):
                server.model_request({'url': url, 'body': '{}'})

    def test_returns_the_model_response(self):
        received = []

        class Model(BaseHTTPRequestHandler):
            def do_POST(self):
                received.append(self.rfile.read(int(self.headers['Content-Length'])).decode())
                body = b'{"message":{"content":"{}"}}'
                self.send_response(200)
                self.send_header('Content-Length', str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def log_message(self, *args):
                pass

        model = HTTPServer(('127.0.0.1', 0), Model)
        threading.Thread(target=model.handle_request, daemon=True).start()
        url = f'http://127.0.0.1:{model.server_address[1]}/api/chat'
        self.assertEqual(server.model_request({'url': url, 'body': '{"model":"m"}'}),
                         '{"message":{"content":"{}"}}')
        self.assertEqual(received, ['{"model":"m"}'])
        model.server_close()

    def test_unreachable_host_is_an_error_envelope(self):
        with socket.socket() as probe:
            probe.bind(('127.0.0.1', 0))
            port = probe.getsockname()[1]
        reply = json.loads(server.model_request({'url': f'http://127.0.0.1:{port}/api/chat', 'body': '{}'}))
        self.assertFalse(reply['ok'])
        self.assertIn('model host', reply['error'])


class EditorSessions(unittest.TestCase):
    def test_forward_waits_for_the_page_reply(self):
        sessions = server.Sessions()
        key = sessions.register()['session']

        def page():
            message = sessions.poll(key, timeout=5)[0]
            sessions.reply(key, message['id'], {'status': 'applied', 'packet': message['packet']})

        threading.Thread(target=page, daemon=True).start()
        result = sessions.forward(key, {'operation': 'inspect'}, timeout=5)
        self.assertEqual(result, {'status': 'applied', 'packet': {'operation': 'inspect'}})
        self.assertEqual(sessions.list(), [{'session': key, 'active': True}])
        with self.assertRaises(ValueError):
            sessions.forward('unknown', {})


if __name__ == '__main__':
    unittest.main()
