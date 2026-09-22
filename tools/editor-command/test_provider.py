import os
import json
import unittest
from pathlib import Path
from unittest.mock import patch
import httpx2
from openai import AsyncOpenAI
from contract import Proposal
from provider import generate, settings


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


class LocalWireTests(unittest.IsolatedAsyncioTestCase):
    async def test_mlx_receives_reasoning_and_output_budget_in_supported_fields(self):
        requests = []
        async def respond(request):
            requests.append(json.loads(request.content))
            return httpx2.Response(200, json={'id': 'local-test', 'model': 'gpt-oss', 'object': 'chat.completion', 'created': 1,
                'choices': [{'index': 0, 'finish_reason': 'stop', 'message': {'role': 'assistant',
                    'content': '{"decision":"act","operations":[{"op":"save"}]}'}}]})
        client = AsyncOpenAI(base_url='http://127.0.0.1:11436/v1', api_key='local',
                             http_client=httpx2.AsyncClient(transport=httpx2.MockTransport(respond)))
        def inspect_objects(object_ids: list[str]) -> list[dict]:
            return []
        with patch.dict(os.environ, {'CHIBI_MODEL_BACKEND': 'mlx', 'CHIBI_MODEL': 'gpt-oss', 'CHIBI_REASONING': 'low', 'CHIBI_MODEL_OUTPUT': 'native', 'CHIBI_MODEL_ADAPTER': '/tmp/chibi-test-adapter'}), \
             patch('provider.AsyncOpenAI', return_value=client):
            output, _ = await generate('Use observed actions.', 'Save', Proposal, inspect_objects, None)
        self.assertEqual(output.operations[0].op, 'save')
        self.assertEqual(requests[0]['max_tokens'], 800)
        self.assertNotIn('max_completion_tokens', requests[0])
        self.assertEqual(requests[0]['chat_template_kwargs']['reasoning_effort'], 'low')
        self.assertEqual(requests[0]['response_format']['type'], 'json_schema')
        self.assertEqual(requests[0]['adapters'], str(Path('/tmp/chibi-test-adapter').resolve()))


if __name__ == '__main__':
    unittest.main()
