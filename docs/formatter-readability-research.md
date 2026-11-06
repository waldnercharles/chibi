# Readability and logical code paragraphs

Researched on 2026-11-02 using original papers, author-hosted material and
first-party tool documentation. This note extends the
[formatter option survey](formatter-options-survey.md).

The logical paragraph formatter now implements SEGMENT's four published phases
with documented Chibi completions. The [algorithm reference](formatter-segment-algorithm.md)
separates recovered rules from implementation decisions; the
[implementation record](formatter-implementation.md) records validation and limits.
The original Java source remains unrecovered. A previous custom dynamic-programming
partitioner has been replaced, including its density scores and paragraph-size targets.

The formatter adds blank lines between complete statements using syntax,
lexically resolved variable relationships, and identifier similarity. It is
opt-in and deterministic. Per-boundary explanations and learned classifiers
remain possible additions.

Keep three questions separate when evaluating it:

- Does this presentation help someone scan the code and recognize its sections?
- Does a particular developer prefer it?
- Does it improve correctness or time on a comprehension task?

Those require different evidence. An attractive presentation or a classifier's
high score does not establish improved comprehension.

## Direct precedent for inserting logical separators

Wang, Pollock and Vijay-Shanker's **SEGMENT** is unusually close to this request.
Their journal paper describes automatically inserting blank lines into Java
methods by identifying consecutive statements that implement a higher-level
action. It combines program structure and naming information, and evaluates
the result through human judgments. This is more ambitious than inserting a
blank before every `return` or every loop. [Original paper][segment-paper].

Wang's author-hosted dissertation documents the algorithm in detail. Chapter 6
starts with an AST and variable definitions/uses, identifies action units,
resolves overlaps, refines large syntactically similar groups and merges small
groups. It recurses into compound statement bodies. Similarity uses identifiers
and right-hand-side expressions. [Dissertation, chapter 6][segment-thesis].

Its evaluation also gives a useful warning about over-segmentation. In one
majority-annotator comparison, it recovered 161 of 179 agreed separators but
inserted 234 in total. Some extra separators received minority support; 29 had
no annotator support. The discussion identifies insufficient merging and the
position of a statement within a method as problems. These results support
testing the idea, not copying thresholds as universal rules.
[Evaluation, pages 89–91][segment-thesis].

Dataflow is an appropriate signal. A program dependence graph represents data
and control dependencies between operations. Its original purpose includes
optimization rather than deciding visual paragraphs. Using those relationships
to estimate paragraph cohesion is a Chibi design inference.
[Original dependence-graph paper][pdg].

Dependencies cannot decide everything. In a load, validate, transform and save
pipeline, each stage consumes the previous stage's result. They can still
deserve separate paragraphs. Conversely, independent statements may belong
together because they initialize one conceptual object. Blank lines do not
require those statements to be semantically independent.

## Existing formatter and linter behavior

| Tool | Verified behavior | Useful Chibi direction |
| --- | --- | --- |
| Go `wsl` | Enforces or permits adjacent statement groups using variable relationships. An assignment immediately before an `if` can stay attached when the variable occurs in the condition, with options for first-use/body-use and a maximum number of attached statements. It has separate block, declaration, expression, branch and return rules. [Official checks][wsl]. | Keep a producer and its immediate check together; bound the size of such a group. |
| ESLint Stylistic | `padding-line-between-statements` chooses `always`, `never` or `any` from preceding/following statement kinds. It supports ordered overrides and selectors, and documents grouping declaration runs and separating returns. The corresponding ESLint core rule is deprecated in favor of this plugin. [Current rule][stylistic], [core migration notice][eslint]. | Offer predictable syntax rules before trying inferred topics. |
| Rider | Has separate blank-line counts for declarations, import groups, comments and control-transfer contexts, along with maximum retained blank lines and block-edge rules. [Official schema][rider-blank]. | Separate required gaps from preserved author grouping and maximum whitespace. |
| Prettier | Preserves authored blank lines while collapsing repeated ones and removing block-edge blanks. [Rationale][prettier]. | Treat existing paragraphs as useful author intent. |
| OpenStack Mistral's own style guide | Recommends whitespace around algorithm steps, certain controls and returns, with exceptions for enclosing blocks containing only that construct. [Project guide][mistral]. | A readable profile can deliberately add vertical space using published, explainable conventions. |

The first two provide functioning automation rather than only style advice.
None of these sources establishes that its chosen convention improves every
developer's comprehension. They establish practical policies worth making
available and evaluating.

## What readability models can tell us

| Primary research | What it measures | Implication for this formatter |
| --- | --- | --- |
| Buse and Weimer, *Learning a Metric for Code Readability* | Predicts perceived readability from local features. Its study used 120 students, 100 short Java snippets and 12,000 ratings. The authors explicitly describe the model as descriptive rather than prescriptive and warn that a blank-line correlation does not imply adding many blank lines helps. [Author preprint][buse]. | Useful feature ideas; do not maximize its score to choose formatting. |
| Scalabrino and colleagues, *A Comprehensive Model for Code Readability* | Combines visual/structural features with identifier and comment features. Its combined model improves rating classification on the studied datasets. [Author paper][scalabrino-readability]. | Names, documentation and textual coherence matter, but ordinary formatting cannot repair all of them. |
| Scalabrino and colleagues, *Automatically Assessing Code Understandability* | Evaluates 121 metrics using 444 evaluations from 63 developers. It distinguishes perceived understanding, question correctness and time; the tested metrics and combinations were not sufficiently effective for practical prediction. [Author paper][scalabrino-understanding]. | Rating prediction and actual understanding need separate validation. This does not prove that no useful future metric is possible. |
| Vitale and colleagues, *Personalized Code Readability Assessment: Are We There Yet?* | Studies individual preferences using feature models and personalized LLM prompts. Its results expose the difficulty of learning a person's judgments from a few examples. [Author preprint][personalized]. | Collect Chibi-specific preferences, allow ties and uncertainty, and avoid presenting an aggregate preference as everyone's preference. |

Sonar's Cognitive Complexity counts disruptions to control flow and nesting,
with rules for shorthand and other constructs. A blank-line-only edit normally
leaves that structure unchanged. That makes it a useful separate signal about
control flow, but an unsuitable objective for ranking these layout variants.
The latter conclusion follows from the published rules rather than an
experiment comparing Chibi layouts. [Official specification][cognitive].

## Classifiers and learned formatting

There are two relevant learning problems.

First, a formatter can learn a house style. **CodeBuff** learns formatting
decisions from a representative corpus using syntax/context features. Its
paper and implementation demonstrate this approach. Matching a corpus's
formatting is a different target from proving that its style is easiest to
understand. [Original paper][codebuff-paper], [implementation][codebuff-code].

Second, a model can learn logical boundaries. Dormuth and colleagues'
**Logical Segmentation of Source Code** predicts boundaries with character-based
neural networks across six languages. Its training data concatenates Stack
Overflow snippets and treats the joins as approximate boundaries. The authors
explicitly acknowledge that those artificial joins may not represent real code.
Its high reported newline accuracy therefore cannot be read as the probability
of finding the right paragraphs in a real Chibi function.
[Original paper, sections 3–6][logical-ml].

For Chibi, I would train a boundary classifier before training a whole-function
"readable/unreadable" classifier. Its question is concrete: should this legal
gap between sibling statements start a new paragraph? Input features can include
AST kinds, resolved bindings, local producer/consumer relationships, receiver
changes, comment anchors and the amount of code on either side. A small linear
model or tree could remain inspectable and run in the browser. Those model
choices and deployment properties are proposals requiring measurement.

A general LLM can suggest sections or explain a proposed split, but its output
should not silently determine normal formatting. Pinning a model and decoding
settings, validating the result and collecting human preference data would
still be necessary. In particular, a model's confidence or persuasive explanation
is not an observed probability that developers will read the result better.

## Implemented deterministic algorithm

Public modes:

```text
statement_paragraphs = preserve | syntax | logical
```

`preserve` retains authored grouping. `syntax` adds gaps around controls and
nested declarations such as functions and records. `logical` runs the four SEGMENT phases:

1. Identify maximal syntactically similar runs, object-related candidates,
   compound-statement preambles, and individual returns.
2. Resolve candidate overlap using the published category rules and a fixed
   source-order policy for unspecified cases.
3. Refine syntactically similar runs of at least four statements with the
   published three-statement similarity window.
4. Merge eligible singleton statements using the published cross-category
   compatibility table, trying the previous block before the following block.

The [algorithm reference](formatter-segment-algorithm.md) gives the exact
similarity table, identifier rules, recognition procedure, and merge ordering.
It also records a contradiction between the source's one-word identifier rule
and the literal names in one example. Tests retain the rule and translate that
example's names to preserve its intended similarity contrast.

Chibi's candidate recognition uses consecutive definition/use occurrence runs
for each lexical binding and contiguous dependency preambles. It distinguishes
shadowed bindings and excludes member names and labels from variable reads.
It does not prove alias identity, object effects, or semantic action boundaries.
Explicit type syntax supplies type-name similarity; missing types remain unknown.
There are no tuned weights, name-count caps, density profiles, or paragraph-length
limits in the logical pass.

Function, macro, and lambda bodies are always eligible. Other bodies need at
least four immediate statements, and traversal continues through shorter
ancestors to eligible deeper bodies. The outer pass treats a compound statement
as one unit. Singleton merging also counts AST statements, so physical wrapping
does not change that decision.

Authored gaps, comment attachment, labels, and protected regions constrain the
formatter's output. These are Chibi formatting requirements around SEGMENT,
not extra evidence that a proposed boundary is a logical action. Their
interaction with repeated formatting needs public-formatter validation;
the research description alone does not prove a fixed point.

The earlier design in this note proposed a bounded cost optimizer and
size-driven groups. That design is historical. It is not the implemented
SEGMENT policy or a source of default weights.

## Comparing two presentations

For the same tokens and semantics, show a small set of measurable dimensions
instead of a universal score:

- Maximum visual width and how often it exceeds the selected width.
- Paragraph size distribution and isolated one-statement groups.
- How often a strong producer/check or receiver-run relationship is split.
- Preservation of explicit author sections and comment ownership.
- Vertical expansion, alignment padding and the number of inserted separators.

These describe tradeoffs. More blank lines can improve scanning while pushing
related code off screen. A pairwise preference model can combine the dimensions
for a particular developer or team, with a confidence estimate calibrated on
held-out preferences. A hand-written boundary score should be called evidence
strength rather than a probability.

To evaluate a prototype, collect same-AST Chibi alternatives, randomize left/right
presentation and allow a tie. Cover short functions, long pipelines, setup code,
field initialization, nested controls, data tables, comments and incomplete
drafts. Measure boundary false splits and missed sections, overall preference,
and preservation of author grouping separately. Add actual tracing or change
tasks if claiming better comprehension, recording correctness and time.

For learned versions, split evaluation by repository and function family,
deduplicate cloned examples, and keep alternatives of one function in the same
split. Do not leak the target blank lines into the input features. Evaluate
new developers separately from a personalized model's training participants.
These are proposed safeguards against misleading results, not claims that an
existing model has passed them for Chibi.

## Implementation and remaining evaluation

The implementation lives in [Paragraphs.chibi](../tools/compiler/source_format/Paragraphs.chibi)
and the `SegmentFacts`, `SegmentActions`, `SegmentSimilarity`, and `SegmentRefine`
modules. The facts pass resolves lexical scope and retains every variable
occurrence. Interval and dependency sorting avoid an all-pairs statement graph;
phase scans use flat statement tables. Actual validation results and elapsed
time belong in the [implementation record](formatter-implementation.md).

Preference collection and comprehension experiments remain unimplemented.
Evaluate behavior on real Chibi code before claiming that the Java study's
results transfer to this language or these recognition completions. A
repeatable formatter and agreement with source examples establish implementation
properties; reader preference and comprehension need their own evidence.

## Primary sources

[segment-paper]: https://onlinelibrary.wiley.com/doi/10.1002/smr.1581
[segment-thesis]: https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf
[pdg]: https://www.cs.utexas.edu/~pingali/CS395T/2009fa/papers/ferrante87.pdf
[wsl]: https://github.com/bombsimon/wsl/blob/main/CHECKS.md
[stylistic]: https://eslint.style/rules/padding-line-between-statements
[eslint]: https://eslint.org/docs/latest/rules/padding-line-between-statements
[rider-blank]: https://www.jetbrains.com/help/rider/EditorConfig_CSHARP_BlankLinesPageScheme.html
[prettier]: https://prettier.io/docs/rationale
[mistral]: https://docs.openstack.org/mistral/latest/contributor/coding_guidelines.html
[buse]: https://web.eecs.umich.edu/~weimerw/p/weimer-tse2010-readability-preprint.pdf
[scalabrino-readability]: https://sscalabrino.github.io/files/2018/JSEP2018AComprehensiveModel.pdf
[scalabrino-understanding]: https://sscalabrino.github.io/files/2019/TSE2019AutomaticallyAssessingCode.pdf
[personalized]: https://arxiv.org/html/2503.07870v1
[cognitive]: https://www.sonarsource.com/docs/CognitiveComplexity.pdf
[codebuff-paper]: https://arxiv.org/abs/1606.08866
[codebuff-code]: https://github.com/antlr/codebuff
[logical-ml]: https://arxiv.org/pdf/1907.08615
