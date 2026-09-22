"""Measure the exact pinned Laya serializer before inference can truncate it."""
import hashlib
import json
from laya.common import render_options, serialize_state


def metadata(agent):
    cfg = agent.cfg
    tokenizer = agent.tok.backend_tokenizer.to_str()
    return {'max_len':cfg['max_len'], 'head_max_len':cfg['head_max_len'],
            'max_questions':6, 'max_option_tokens':48,
            'config_sha256':hashlib.sha256(json.dumps(cfg,sort_keys=True).encode()).hexdigest(),
            'tokenizer_sha256':hashlib.sha256(tokenizer.encode()).hexdigest(),
            'encoder':cfg.get('encoder'), 'device':str(agent.device)}


def measure(agent, state, questions):
    tok, cfg = agent.tok, agent.cfg
    def length(text):
        return len(tok(str(text).replace(tok.mask_token,' '),add_special_tokens=False)['input_ids'])
    state_count = length(serialize_state(state))
    rows = {}
    for key, question in questions.items():
        q = {'t':question['type'],'ins':question['instructions'],'crit':question.get('criteria')}
        instruction = q['ins'] if isinstance(q['ins'],str) else json.dumps(q['ins'])
        head_count = length(f"{q['t']} question: {instruction}")
        options = [length(' ' + text) for text in render_options(q)]
        # Include MASK markers and CLS/SEP tokens, exactly as build_sequence.
        option_budget = sum(1 + n for n in options)
        head_budget = cfg['head_max_len'] - option_budget
        sequence = 1 + head_count + 1 + option_budget + 1 + state_count + 1
        truncated = any(n > 48 for n in options) or head_budget < 16 or head_count > max(8,head_budget) or sequence > cfg['max_len']
        rows[key] = {'instruction_tokens':head_count,'option_tokens':options,'state_tokens':state_count,'sequence_tokens':sequence,'truncated':truncated}
    if len(questions) > 6:
        raise ValueError('Use at most six independent questions')
    if any(row['truncated'] for row in rows.values()):
        raise ValueError('Decision exceeds the model token budget; shorten context or candidates')
    return rows
