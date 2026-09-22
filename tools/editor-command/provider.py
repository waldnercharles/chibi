"""Local structured generation; PydanticAI owns tool dispatch and schema retries."""
import asyncio
import os
import json
from dataclasses import asdict
from urllib.parse import urlsplit

os.environ.setdefault('PYDANTIC_AI_NO_BANNER', '1')

from openai import AsyncOpenAI
from pydantic_ai import Agent, ModelRetry, NativeOutput, UsageLimits
from pydantic_ai.models.ollama import OllamaModel
from pydantic_ai.providers.ollama import OllamaProvider


def settings():
    url = os.environ.get('CHIBI_MODEL_URL', 'http://127.0.0.1:11435/v1')
    model = os.environ.get('CHIBI_MODEL', 'gpt-oss:20b')
    parsed = urlsplit(url)
    if parsed.scheme != 'http' or parsed.hostname not in {'127.0.0.1', 'localhost', '::1'} or parsed.username or parsed.password:
        raise ValueError('The command model must use a local loopback URL')
    if 'cloud' in model.lower():
        raise ValueError('The command model must be local')
    return url, model


async def generate(instructions, prompt, output_type, inspect_objects, validate):
    url, name = settings()
    async with AsyncOpenAI(base_url=url, api_key='local', max_retries=0, timeout=18) as client:
        model = OllamaModel(name, provider=OllamaProvider(openai_client=client))
        instructions += '\nReturn JSON matching this schema:\n' + json.dumps(output_type.model_json_schema(), separators=(',', ':'))
        agent = Agent(model, instructions=instructions, output_type=NativeOutput(output_type, strict=False),
                      tools=[inspect_objects], retries=1,
                      model_settings={'temperature': 0, 'presence_penalty': 0, 'max_tokens': 800,
                                      'openai_reasoning_effort': 'low' if name.startswith('gpt-oss') else 'none'})
        if validate:
            @agent.output_validator
            def check(output):
                if output.decision == 'act' and not output.question and not output.unsupported:
                    try:
                        validate(output.operations)
                    except ValueError as error:
                        raise ModelRetry(str(error)) from error
                return output
        result = await asyncio.wait_for(agent.run(prompt, usage_limits=UsageLimits(request_limit=3)), 22)
        return result.output, {'model': name, 'usage': asdict(result.usage)}


def ask(instructions, prompt, output_type, inspect_objects, validate=None):
    return asyncio.run(generate(instructions, prompt, output_type, inspect_objects, validate))
