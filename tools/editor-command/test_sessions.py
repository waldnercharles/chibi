import concurrent.futures
import time
import unittest
from sessions import Sessions


class SessionsTests(unittest.TestCase):
    def test_roundtrip_only_the_selected_live_session(self):
        sessions=Sessions()
        first=sessions.register()['session']
        second=sessions.register()['session']
        with concurrent.futures.ThreadPoolExecutor() as pool:
            future=pool.submit(sessions.forward,first,{'operation':'inspect'})
            message=sessions.poll(first)[0]
            self.assertEqual(message['packet'],{'operation':'inspect'})
            sessions.reply(second,message['id'],{'wrong':True})
            self.assertFalse(future.done())
            sessions.reply(first,message['id'],{'revision':'live'})
            self.assertEqual(future.result(timeout=1),{'revision':'live'})

    def test_disconnected_and_unknown_sessions_reject(self):
        sessions=Sessions()
        with self.assertRaises(ValueError):sessions.forward('unknown',{})
        key=sessions.register()['session']
        sessions.sessions[key]['seen']=time.monotonic()-60
        with self.assertRaises(ValueError):sessions.forward(key,{})


if __name__=='__main__':unittest.main()
