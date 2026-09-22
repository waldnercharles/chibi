"""Local structured generation; PydanticAI owns tool dispatch and schema retries."""
import asyncio
import os
import json
from dataclasses import asdict
from pathlib import Path
from urllib.parse import urlsplit

os.environ.setdefault('PYDANTIC_AI_NO_BANNER', '1')

from openai import AsyncOpenAI
from pydantic_ai import Agent, ModelRetry, NativeOutput, PromptedOutput, UsageLimits
from pydantic_ai.models.openai import OpenAIChatModel
from pydantic_ai.profiles.openai import OpenAIModelProfile
from pydantic_ai.models.ollama import OllamaModel
from pydantic_ai.providers.ollama import OllamaProvider
from pydantic_ai.providers.openai import OpenAIProvider


def settings():
    backend = os.environ.get('CHIBI_MODEL_BACKEND', 'ollama')
    if backend not in {'ollama', 'mlx'}:
        raise ValueError('Choose the ollama or mlx local backend')
    default_model = 'gpt-oss:20b' if backend == 'ollama' else str(Path(__file__).resolve().parents[2] / '.chibi/local-model/mlx-gpt-oss')
    url = os.environ.get('CHIBI_MODEL_URL', f'http://127.0.0.1:{11435 if backend == "ollama" else 11436}/v1')
    model = os.environ.get('CHIBI_MODEL', default_model)
    parsed = urlsplit(url)
    if parsed.scheme != 'http' or parsed.hostname not in {'127.0.0.1', 'localhost', '::1'} or parsed.username or parsed.password:
        raise ValueError('The command model must use a local loopback URL')
    if 'cloud' in model.lower():
        raise ValueError('The command model must be local')
    return url, model


def output_schema(output_type):
    return json.dumps(output_type.model_json_schema(), separators=(',', ':'))


async def generate(instructions, prompt, output_type, inspect_objects, validate):
    url, name = settings()
    backend = os.environ.get('CHIBI_MODEL_BACKEND', 'ollama')
    effort = os.environ.get('CHIBI_REASONING', 'low' if 'gpt-oss' in name else 'none')
    if effort not in {'low', 'medium', 'high', 'none'} or ('gpt-oss' in name and effort == 'none'):
        raise ValueError('GPT-OSS reasoning must be low, medium or high')
    async with AsyncOpenAI(base_url=url, api_key='local', max_retries=0, timeout=18) as client:
        model_settings = {'temperature': 0, 'presence_penalty': 0, 'max_tokens': 800}
        if backend == 'mlx':
            # vllm-mlx owns Harmony tool parsing and constrained JSON decoding.
            model = OpenAIChatModel(name, provider=OpenAIProvider(openai_client=client),
                                    profile=OpenAIModelProfile(supports_json_schema_output=True,
                                                              supports_json_object_output=False,
                                                              openai_chat_supports_max_completion_tokens=False))
            model_settings['extra_body'] = {'chat_template_kwargs': {'reasoning_effort': effort}}
            if adapter := os.environ.get('CHIBI_MODEL_ADAPTER'):
                model_settings['extra_body']['adapters'] = str(Path(adapter).resolve())
        else:
            model = OllamaModel(name, provider=OllamaProvider(openai_client=client))
        output_mode = os.environ.get('CHIBI_MODEL_OUTPUT', 'native')
        if output_mode not in {'native', 'prompted'}:
            raise ValueError('Choose native or prompted structured output')
        output = NativeOutput(output_type, strict=False) if output_mode == 'native' else PromptedOutput(output_type, template=False)
        model_settings['openai_reasoning_effort'] = effort
        instructions += '\nReturn JSON matching this schema:\n' + output_schema(output_type)
        agent = Agent(model, instructions=instructions, output_type=output,
                      tools=[inspect_objects], retries=1, model_settings=model_settings)
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
        return result.output, {'model': name, 'backend': backend, 'reasoning': effort, 'output': output_mode, 'usage': asdict(result.usage)}


def ask(instructions, prompt, output_type, inspect_objects, validate=None):
    return asyncio.run(generate(instructions, prompt, output_type, inspect_objects, validate))
