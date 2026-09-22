from types import SimpleNamespace
import pytest
from token_budget import measure


class Tokenizer:
    mask_token = '[MASK]'
    def __call__(self, text, add_special_tokens=False):
        return {'input_ids': list(range(len(text.split())))}


def agent(max_len=512, head=192):
    return SimpleNamespace(tok=Tokenizer(), cfg={'max_len':max_len,'head_max_len':head})


def question(options):
    return {'type':'choice','instructions':'Choose a field','criteria':options}


def test_complete_request_reports_each_head():
    result=measure(agent(),'set width to 30',{'field':question({'0':'Width','1':'Height','2':'None'})})
    assert result['field']['state_tokens']==4
    assert len(result['field']['option_tokens'])==3
    assert not result['field']['truncated']


def test_long_option_cannot_be_silently_truncated():
    with pytest.raises(ValueError,match='token budget'):
        measure(agent(),'short',{'field':question({'0':'word '*60,'1':'None'})})


def test_state_or_aggregate_head_overflow_rejects():
    with pytest.raises(ValueError,match='token budget'):
        measure(agent(max_len=24),'word '*25,{'field':question({'0':'Width','1':'Height'})})
    with pytest.raises(ValueError,match='token budget'):
        measure(agent(head=24),'short',{'field':question({str(i):'word '*10 for i in range(5)})})


def test_too_many_independent_heads_rejects():
    with pytest.raises(ValueError,match='six'):
        measure(agent(),'short',{str(i):question({'0':'Width','1':'None'}) for i in range(7)})
