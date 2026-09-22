# GPT-OSS residency and smaller-model pilot — 2026-09-22

## Cold versus warm

Apple M4 Pro, 48 GB unified memory, Ollama 0.34.2, GPT-OSS 20B MXFP4,
8192 context, full-precision KV cache, low reasoning, one model and one request
slot. Nine measurements per condition: three requests, each repeated three times.
Every proposal matched its complete expected operations; no edits were applied.

| Request | Model unloaded | Preloaded, fresh prompt | Immediate repeat |
|---|---:|---:|---:|
| Increase prototype width to 125% | 7.10 s | 4.77 s | 2.35 s |
| Hide Inspector | 7.19 s | 5.09 s | 2.03 s |
| Change two emitter colors, richer catalog | 9.63 s | 7.47 s | 3.40 s |
| Median across all nine trials | **7.50 s** | **5.09 s** | **2.35 s** |

Preloading reduced the aggregate median by 32% (1.47× faster).
Immediate repeats reduced it by 69% (3.20×), combining model residency with prompt
cache reuse. These ratios are latency ratios, not measured concurrent throughput.
The earlier 3.45 s tuning median was already warm; it did not include cold load.

Each unloaded request followed an explicit Ollama unload and an empty `/api/ps`
check. The resident/fresh condition also began with unload, then an empty preload,
before sending the command. Unloaded and preloaded condition order was shuffled;
immediate repeats necessarily followed the preloaded command. Python dependencies
were imported before timing. The first unloaded width request took 14.66 s;
subsequent unloaded trials were 6.83–9.86 s. OS filesystem caches were not flushed:
this is model reload, not machine reboot or first-ever download.

Timing covers resolution, model calls, tool calls, and validation. It excludes
editor apply/render, human review, and preload time in resident conditions. An
unchanged repeated request gives unusually favorable prompt reuse; ordinary edits
change the observation. Raw results, source hashes, case hashes and model residency
snapshots are in [residency-results.json](residency-results.json).

## Prompt comparison

Same 14 requests, two shuffled passes per configuration. Runs were sequential,
using the same model/runtime settings. No training or model downloads ran during
these latency comparisons. These are small regression comparisons, not confidence
intervals or a general accuracy estimate.

| Configuration | Correct complete results | Median | p95 |
|---|---:|---:|---:|
| Original prompt | 28/28 | 3.63 s | 7.15 s |
| Over-compressed schema experiment, rejected | 24/28 | 3.21 s | 6.91 s |
| Retrieved properties, full schema, concrete copy example; later rejected | 28/28 | 3.18 s | 5.29 s |

The second candidate reduced median latency by 12% and p95 by 26%. Mean input
tokens dropped from 2370 to 2036; mean generated tokens from 79 to 68. Total model
calls fell from 31 to 30 for 28 commands. Output allowance remains 800 tokens and
the validated repair path remains available; plans are not truncated to manufacture
a speedup.

Property retrieval uses reflected field names. The complete identity directory
remains visible, including ambiguity evidence. Read-only inspection supplies
omitted fields, and validation rejects unobserved fields even when their object
was already seen. When no property name matches, all properties are provided.
This is a bounded optimization; very large object directories still need retrieval.

Removing schema titles/defaults in the rejected experiment coincided with two copy
reference errors and two panel tool failures. The retained version restores the
full schema, adds a generic duplicate-reference example, distinguishes panel IDs
from object IDs in inspection guidance, and makes invalid inspection IDs eligible
for the existing bounded repair. Multiple changes were tested together, so these
results do not isolate each change's contribution. The broader 74-case run then
caught an accuracy regression (54/62 supported requests correct), especially
choosing an instance over an exact prototype name. The concrete object IDs in the
copy example were removed; final results are reported separately below.

## Reproduction

Commands and local training setup are in [README.md](../README.md). Keep the
palette idle during residency tests. The launcher now preloads GPT-OSS and defaults
to `OLLAMA_KEEP_ALIVE=-1`; an already running server must be restarted to change its
default. Keeping GPT-OSS loaded occupies about 12 GB of unified memory on this host.

References: [Ollama residency settings](https://docs.ollama.com/faq),
[GPT-OSS model](https://developers.openai.com/api/docs/models/gpt-oss-20b),
[MLX LoRA/QLoRA](https://github.com/ml-explore/mlx-lm/blob/main/mlx_lm/LORA.md),
[pinned small-model family](https://huggingface.co/mlx-community/Qwen3-4B-Instruct-2507-4bit).
