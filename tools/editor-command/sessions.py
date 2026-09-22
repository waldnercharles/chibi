"""Authenticated server callers forward commands to a registered live browser."""
import queue
import secrets
import threading
import time


class Sessions:
    def __init__(self):
        self.lock = threading.Lock()
        self.sessions = {}

    def register(self):
        with self.lock:
            self.sessions = {k: v for k, v in self.sessions.items() if time.monotonic() - v['seen'] < 60}
            if len(self.sessions) >= 16:
                raise ValueError('Too many editor sessions')
            key = secrets.token_hex(16)
            self.sessions[key] = {'seen': time.monotonic(), 'queue': queue.Queue(), 'waiting': {}}
            return {'session': key}

    def list(self):
        with self.lock:
            return [{'session': k, 'active': time.monotonic() - v['seen'] < 30} for k, v in self.sessions.items()]

    def get(self, key):
        session = self.sessions.get(key)
        if not session:
            raise ValueError('Unknown editor session')
        return session

    def poll(self, key):
        with self.lock:
            session = self.get(key)
            session['seen'] = time.monotonic()
        try:
            return [session['queue'].get(timeout=15)]
        except queue.Empty:
            return []

    def reply(self, key, request, result):
        with self.lock:
            channel = self.get(key)['waiting'].get(request)
            if channel:
                channel.put_nowait(result)
        return {'ok': True}

    def forward(self, key, packet):
        with self.lock:
            session = self.get(key)
            if time.monotonic() - session['seen'] > 30:
                raise ValueError('Editor session disconnected')
            request = secrets.token_hex(12)
            channel = queue.Queue(maxsize=1)
            session['waiting'][request] = channel
            session['queue'].put({'id': request, 'packet': packet})
        try:
            return channel.get(timeout=10)
        except queue.Empty:
            # A timed-out submit must be polled using its request_id; never retry with a new ID.
            raise ValueError('Editor did not respond; inspect the task using the same request_id') from None
        finally:
            with self.lock:
                session['waiting'].pop(request, None)
