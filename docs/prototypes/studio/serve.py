#!/usr/bin/env python3
"""Serve the repository root without caching so study edits appear on reload."""
import sys
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class NoCache(SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')
        super().end_headers()


if __name__ == '__main__':
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8140
    root = Path(__file__).resolve().parents[3]
    print(f'http://127.0.0.1:{port}/docs/prototypes/studio/', flush=True)
    ThreadingHTTPServer(('127.0.0.1', port), partial(NoCache, directory=root)).serve_forever()
