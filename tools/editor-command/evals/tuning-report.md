# Local command runtime and retrieval comparison

Measured on 2026-09-21 on an Apple M4 Pro with 48 GB unified memory.
Raw proposals, outcomes, timings, source hashes and model revisions are in
[tuning-results.json](tuning-results.json). No shadow proposal was executed.
The 19 focused Python tests passed. A separate live editor verified Cmd+K resolving
"Set the BoostEmitter prototype rate to 23", Apply, field readback and guarded
Undo restoring the original value. That live edit took 5.29 seconds before Undo.
No native code changed in this experiment; the full engine suite was not rerun.

## Ollama settings

The baseline uses Ollama 0.34.2, GPT-OSS 20B with MXFP4 expert weights, full GPU
offload, low reasoning effort, an 8192-token context, one parallel request and
a 30-minute keep-alive. Flash Attention is automatic when supported. The
comparison explicitly enabled Flash Attention and changed KV cache storage
from f16 to q8_0. The comparison server confirmed both settings in its log.

Fourteen requests were run twice in a fixed shuffled order per round. Cases
cover arithmetic, text, colors, graph connections, copying, follow-ups,
navigation and unsupported requests. Five cases use captured live catalogs;
the rest use the frozen fixture. Model warm-up is excluded. Timings include
retrieval, generation, inspection calls, repair retries and validation; they
exclude editor application, rendering and human review.

| Configuration | Complete correct results | Median | p95 |
| --- | ---: | ---: | ---: |
| Ollama, f16 cache | 28/28 | 3.45 s | 7.71 s |
| Ollama, q8_0 cache | 26/28 | 3.34 s | 6.70 s |

Both cache failures duplicated Marker 1 but moved the original instead of the
copy. A 3% median improvement does not justify this regression. Keep f16.
These are small regression measurements, not estimates of general accuracy.
Configuration runs were sequential rather than interleaved; model downloading
overlapped the initial runs. The source gained backend selection between them,
but the Ollama request schema, instructions, sampling and limits were unchanged.

The launcher now accepts keep-alive and context overrides. `OLLAMA_KEEP_ALIVE=-1`
can remove later model reloads at the cost of retaining roughly 12 GB of model
memory. It does not make an already-warm request faster. Low reasoning, short
answers and one parallel request are already in use. Reducing an output limit
only helps when generation reaches that limit, and may truncate the transaction.

## MLX screening

Model: `mlx-community/gpt-oss-20b-MXFP4-Q8`, revision
`773a7da77e569019bb0fd17a554b263738d669a3`. This retains MXFP4 experts and uses
Q8 for other layers; the Ollama baseline was already quantized.

The basic mlx-lm 0.31.3 server did not recognize this checkpoint's Harmony tool
format and returned channel tags as content. That run was stopped; its failures
are a serving incompatibility, not evidence of model intelligence.

The maintained vllm-mlx 0.4.1 server with OpenAI Harmony 0.0.8 handled the protocol.
However, its simple engine reported that system prompt caching was disabled for
this model's rotating cache and Harmony rendering path. A warmed panel request
took 15.52 seconds with constrained output, versus 10.30 seconds with schema
prompting and the same Pydantic validation. Prompted output was used for a bounded
three-case screen; the smaller screen was rerun against Ollama for comparison.

| Same request | Ollama native JSON | MLX prompted JSON |
| --- | ---: | ---: |
| Hide Inspector | 3.11 s | 9.31 s |
| Two colors in the live catalog | 6.45 s | 14.64 s |
| Set width to 125% | 3.90 s | 15.06 s |

All six proposals were correct. The runtimes use different prompt rendering,
cache behavior and output enforcement; this measures usable command latency,
not isolated kernel performance. The MLX screen was too slow to justify running
the full regression suite or switching the editor. Both runtimes were warm;
their approximately 16-second warm-up calls are recorded separately.

## Laya and ColBERT

Laya uses the existing Rancher CPU service, ModernBERT-large encoder, 512-token
budget. Candidates are complete native actions generated from observed metadata:
open, select, frame, panel visibility, playback, save and history. Arbitrary
property values and multi-step plans are deliberately outside this finite menu.
The same descriptions are used for both retrieval methods. Every decision offers
an additional defer option; a singleton shortlist never bypasses Laya.

ColBERT uses `answerdotai/answerai-colbert-small-v1` (33M parameters), revision
`934fa8bb4ce2284f4c2baa232d81aca4d076fa5e`, through Sentence Transformers 6.1.0
MultiVectorEncoder. It loads the learned 96-dimensional projection, uses token
MaxSim, caches document embeddings, and encodes queries on CPU with four threads.
Model initialization took 0.87 s after imports; indexing took 0.031 s. Warm
query encoding and ranking added a median 13.1 ms. Process import time is not
included because a deployed retriever would remain resident.

There are 36 requests: 22 have one eligible action and 14 require deferral because
they are ambiguous, unsupported, conditional or require multiple actions. Each
request was tested with both retrievers at k=1,2,3,5, with configuration order
shuffled per request: 288 decisions total. k excludes the defer option.
Latency includes query retrieval and Laya; it excludes a subsequent GPT-OSS
fallback. No model errors or token-budget rejections occurred.

| Retrieval | k | Correct candidate present /22 | Correct actions /22 | Wrong actions /36 | Correct deferrals /14 | Median |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Lexical | 1 | 19 | 19 | 12 | 5 | 0.370 s |
| Lexical | 2 | 22 | 21 | 12 | 3 | 0.389 s |
| Lexical | 3 | 22 | 20 | 13 | 3 | 0.404 s |
| Lexical | 5 | 22 | 20 | 14 | 2 | 0.436 s |
| ColBERT | 1 | 21 | 21 | 9 | 6 | 0.384 s |
| ColBERT | 2 | 22 | 19 | 14 | 3 | 0.408 s |
| ColBERT | 3 | 22 | 20 | 14 | 2 | 0.433 s |
| ColBERT | 5 | 22 | 20 | 15 | 1 | 0.456 s |

Smaller menus helped, and ColBERT improved the single-candidate ranking. The
earlier inference that perfect shortlist recall meant retrieval could not help
was too strong: candidate competition matters. The remaining nine errors at
ColBERT k=1 prevent automatic execution. Removing alternatives can also conceal
ambiguity; a candidate match does not establish that the entire request is met.

Confidence scores changed substantially with shortlist size. At k=1, no proposal
exceeded 0.8; at ColBERT k=5, 14 correct proposals and no wrong proposals exceeded
0.8 on this same exploratory set. That is not an independently validated threshold.
Do not tune and certify a gate on the same 36 cases. Laya remains offline shadow
only; ColBERT is installed for experiments, not inserted into every Cmd+K request.

## Next accuracy and speed work

1. Use retrieval to reduce the object/property context sent to GPT-OSS while
   retaining explicit identity, ambiguity evidence and an inspection escape path.
   Keep schema and instructions stable to preserve prefix cache reuse. Measure
   complete-request accuracy and latency together against these baselines.
2. Collect reviewed request + observation + complete action/clarification/deferral
   examples. Include partial-request traps, wrong kinds, near-duplicate names,
   follow-ups, conditions, and graph tasks. Successful application alone is not a
   semantic label. Split by scene, names and request families before training;
   reserve new tests instead of training on the benchmarks reported here.
3. Try LoRA/QLoRA on a smaller local generative model, training compact typed plans
   and appropriate deferral. A smaller model with learned editor behavior could
   improve both latency and accuracy. LoRA on GPT-OSS may improve correctness and
   reduce retries/output length, but does not reduce the base model's per-token
   compute. Merging an adapter avoids adapter overhead, not base-model inference.
4. Test a learned Laya execution gate with ColBERT, including fresh ambiguous and
   unsupported examples. Promote a narrow fast path only after independent error
   and coverage measurements. Keep the native revision, transaction, readback and
   Undo contract shared across every model.

GPT-OSS LoRA has an official Transformers/PEFT recipe. It uses an H100 80 GB in
the example; that is not proof that this exact training job fits this Mac. MLX-LM
offers local LoRA/QLoRA, but training and export support must be checked for the
chosen model and quantization. Ollama serves the resulting supported weights;
it is not the training engine. No fine-tuning was performed in this experiment.

Sources: [Ollama settings](https://docs.ollama.com/faq),
[MLX model](https://huggingface.co/mlx-community/gpt-oss-20b-MXFP4-Q8),
[vllm-mlx](https://github.com/waybarrios/vllm-mlx/releases),
[ColBERT model and API](https://huggingface.co/answerdotai/answerai-colbert-small-v1),
[GPT-OSS LoRA recipe](https://developers.openai.com/cookbook/articles/gpt-oss/fine-tune-transfomers),
[MLX LoRA/QLoRA](https://github.com/ml-explore/mlx-lm/blob/main/mlx_lm/LORA.md).
