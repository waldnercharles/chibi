# Laya decision guidance and GPT-OSS speed options

Date: 2026-09-22

## Finding

Laya was not dismissed too quickly, but the useful improvement is in the
decision contract rather than in swapping to the typed-decisions checkpoint.
The base English checkpoint is a good bounded action gate when the question
describes the user intent and each option describes the complete action. It is
not a replacement for GPT-OSS when the command needs new text, multi-step
planning, or an unfamiliar graph operation.

GPT-OSS 20B remains the default for those open-ended editor commands. Its
latency work should continue with prompt and round-trip reduction first, then a
local llama.cpp speculative-decoding comparison.

## Laya evidence

The existing supervised editor service compared a generic next-click question
with a structured asset-type question. On the held-out menu-order cases, the
generic question accepted 4/18 decisions above the confidence threshold; the
structured question accepted 13/18. Both had 0 accepted wrong decisions.
See [the supervised Laya report](laya-editor-supervision.md).

I also ran the same 24-case corpus directly against `laya==0.3.5` and the
downloaded checkpoints. These numbers are exploratory because the direct run
does not use the service container, but the comparison isolates the question
design:

| Question design | Checkpoint | Held-out correct | Held-out accepted | Accepted errors |
| --- | --- | ---: | ---: | ---: |
| Plain labels | Base English | 16/18 | 18/18 | 2 |
| Semantic action descriptions | Base English | 18/18 | 18/18 | 0 |
| Semantic descriptions plus `defer` | Base English | 18/18 | 8/18 | 0 |
| Semantic action descriptions | Typed-decisions | 18/18 | 0/18 | 0 |

The explicit `defer` option correctly covered the supported and unsupported
challenge cases at the tested threshold: 8/12 challenge decisions were
correct, with no accepted errors. It reduces coverage because abstention is
deliberately easier than a confident choice. Adding a second independent Laya
gate made the result worse, so extra questions should not be stacked without
an independent evaluation.

The practical Laya contract is:

- ask for the complete intent, not the next UI gesture;
- describe options by their user-visible action and keep them semantically
  disjoint;
- include an explicit defer or ambiguous option when the caller can safely
  fall back;
- keep the state compact and stable; extra invented workflow metadata reduced
  accuracy in the direct test;
- retain stale-state checks, a small action budget, postcondition readback,
  repeat detection, and fallback in the executor.

The current six-option limit and hierarchical grouping remain appropriate.
ColBERTv2-style retrieval can help select a small candidate set in a large
project, but it does not supply planning, argument construction, or
postcondition verification. The current small JaColBERTv2.5-style retriever
already improved candidate selection in the local harness; replacing it with
classic ColBERTv2 is unlikely to change the bounded-menu decision enough to
justify the added system complexity.

## GPT-OSS speed work

Existing local measurements show that GPT-OSS 20B is the strongest tested
editor model. It benefits substantially from staying resident, using low
reasoning, retaining the full schema, and retrieving only relevant property
context. The f16 KV-cache path is the accuracy baseline; q8 reduced latency
slightly but failed two of 28 cases. See [the tuning report](../../tools/editor-command/evals/tuning-report.md)
and [the model-selection report](../../tools/editor-command/evals/model-selection-research.md).

Speculative decoding is worth testing, with two constraints:

1. The local Ollama GPT-OSS package has no embedded draft layer, so its
   `draft_num_predict` setting does not currently provide an external EAGLE3
   draft for this model.
2. The local llama.cpp runtime supports GPT-OSS Harmony structured output and
   EAGLE3. The Red Hat GPT-OSS 20B speculator is target-specific, so it is the
   right draft to benchmark. Its published acceptance lengths are from A100
   workloads and are not a Mac speed claim.

The meaningful comparison is end-to-end latency and correctness on the
existing editor corpus, while recording prompt/prefill time, decode time,
accepted draft tokens, output length, and peak memory. Short JSON/tool
responses may be decode-bound; if prefill dominates, speculation will have
little effect. Harmony formatting, JSON schema enforcement, and native tool
calls must remain enabled during the comparison.

The first local comparison used the same 14-case tuning corpus, the same
GPT-OSS 20B MXFP4 weights, low reasoning, native JSON schema output, and one
warm-up request:

| Runtime | Supported resolutions | Wrong automatic | Failed | p50 | p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| llama.cpp target only | 11/12 | 0 | 1 | 1.71s | 4.32s |
| llama.cpp plus GPT-OSS EAGLE3 | 10/12 | 1 | 1 | 3.17s | 8.60s |

The EAGLE3 sidecar loaded and produced draft-acceptance telemetry, but
acceptance varied from about 0.14 to 0.60 on these short structured requests.
Metal target throughput also fell after adding the sidecar. This is a usable
reference implementation, yet it is not a speed win for the current editor
shape. Keep speculative decoding as a regression benchmark if output lengths
or model/runtime settings change; do not make it the default based on this
result.

The current provider adapter also needs an explicit llama.cpp native-output
mode before this can become a product path. The native JSON-schema mode worked
in the comparison; the existing prompted and tool modes did not parse reliably
against this server configuration.

Reducing the llama.cpp slot from 8,192 to 4,096 tokens also regressed this
corpus: p50 moved to 2.29s and p95 to 5.75s, with the same one wrong proposal
and one failed clarification. Keep the larger slot until prompt-prefill and
KV behavior are measured on a broader workload.

## Sources

- [Laya base model card](https://huggingface.co/convaiinnovations/laya)
- [Laya typed-decisions model card](https://huggingface.co/convaiinnovations/laya-typed-decisions)
- [ColBERTv2 paper](https://arxiv.org/abs/2112.01488)
- [GPT-OSS announcement](https://openai.com/index/introducing-gpt-oss/)
- [GPT-OSS repository](https://github.com/openai/gpt-oss)
- [llama.cpp speculative decoding](https://github.com/ggml-org/llama.cpp/blob/master/docs/speculative.md)
- [GPT-OSS 20B EAGLE3 draft](https://huggingface.co/RedHatAI/gpt-oss-20b-speculator.eagle3)
