# SEGMENT algorithm reference

The logical paragraph mode implements SEGMENT's four published phases with explicit Chibi rules for cases the recovered description leaves open. It replaces the custom paragraph-cost optimizer. Similarity levels are categorical rules; there are no tuned weights, density scores, target paragraph lengths, or predecessor limits.

The implementation is split between [statement facts](../tools/compiler/source_format/SegmentFacts.chibi), [action recognition and overlap](../tools/compiler/source_format/SegmentActions.chibi), [similarity](../tools/compiler/source_format/SegmentSimilarity.chibi), [refinement and merging](../tools/compiler/source_format/SegmentRefine.chibi), and the [formatter wrapper](../tools/compiler/source_format/Paragraphs.chibi).

## Sources and reproduction limits

The original publication is Wang, Pollock, and Vijay-Shanker, [Automatic Segmentation of Method Code into Meaningful Blocks: Design and Evaluation](https://onlinelibrary.wiley.com/doi/10.1002/smr.1581), first published February 2013 and printed in January 2014. The accessible primary source used below is Wang's [2017 dissertation](https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf), chapter 6, printed pages 75–91. PDF page numbers are printed page numbers plus 11.

Chapter 6 describes SEGMENT directly. Chapters 3 and 5 supply action-unit definitions, but chapter 5 also contains later object-description research. It must not be silently represented as the complete original 2013 implementation. The journal's full text and original Java source have not been recovered. Consequently, this reference distinguishes published rules from decisions needed to make a deterministic Chibi implementation.

The author's group [bibliography](https://sites.google.com/site/asergrp/bibli/software-artifact-summarization) links a [journal PDF](http://servo.cs.wlu.edu/pubs/bitstream/handle/id/284/paper.pdf?sequence=1), [WCRE 2011 PDF](http://servo.cs.wlu.edu/pubs/bitstream/handle/id/248/PID2030167.pdf?sequence=1), and [conference slides](http://servo.cs.wlu.edu/pubs/bitstream/handle/id/248/slides-wcre-2011.pdf?sequence=3). These hosts timed out during this investigation. The author's Delaware site returned HTTP 403. GitHub repository searches and web searches for the author, SEGMENT, and blank-line insertion did not identify an original source archive. The article's generic SourceForge reference is not evidence that SEGMENT source was published there.

The author's [archived July 2018 homepage](https://web.archive.org/web/20180720001100/https://www.eecis.udel.edu/~xiwang/) links the journal as `seg2.pdf` and conference paper as `seg1.pdf`. Those links currently return HTTP 403, and Wayback's index did not contain successful PDF captures. The recovered 2015 and 2018 homepages contain no SEGMENT source download link.

## Input and recursion

Process an ordered list of immediate AST statements and their variable definitions and uses. A compound statement is one statement in its enclosing body. Apply the four phases to the method body, then recursively to compound bodies with at least four immediate statements. Four is the published recursion threshold, not a configurable paragraph-size target. See [dissertation §6.3, pp. 76–77](https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf#page=87).

Chibi always processes function, macro, and lambda bodies. Other statement bodies produce new logical boundaries only with at least four immediate statements. Traversal continues through shorter ancestors, so an eligible deeper body is still processed. A compound statement counts once in its enclosing sequence; a multiline expression also counts once. Comments and blank lines are not statements.

The wrapper preserves authored gaps and protected bytes, keeps comments attached, and restricts insertion around leading comments, labels, and `fmt: off` regions. These restrictions also prevent singleton merging across the affected boundaries. Their interaction with Phase III windows is formatter behavior, not a published SEGMENT rule. Fixed-point behavior must be checked through the public formatter after wrapping and other layout options run.

## Phase I: identify candidate units

Recognize these units independently, allowing shared boundary statements:

| Unit | Published definition |
| --- | --- |
| SynS | Consecutive statements of the same syntax category. |
| Object-related | Consecutive unstructured statements linked through an object. The general template is an optional definition/assignment of `o`, optional calls on `o`, then an optional use of `o`. |
| E-SWIFT | A compound statement and immediately preceding statements that define or initialize a variable used by its controlling condition. The thesis also includes a `try` example whose preamble supplies a value used in the body. |
| Return | Each return initially forms its own unit. |

Syntax categories are `init`, `declare`, `assign`, direct call, object call, and other. An initialized declaration is `init`; an uninitialized declaration is `declare`; assignment to a variable or property is `assign`. These remain distinct even in the function preamble. See [§6.3.1 and table 6.1, pp. 77–78](https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf#page=88).

The object template's terminal use means occurrence on an assignment/initialization RHS or use as an argument to a call on a different receiver. A receiver call is the middle part. The source requires consecutiveness but does not supply the original interval-construction algorithm. See [§3.3.1, pp. 17–18](https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf#page=28). The later multiple-object and focal-statement templates in [§5.4, pp. 56–61](https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf#page=67) are not implemented here as a semantic action recognizer.

Chibi resolves lexical binding identities, including shadowing, loop bindings, and pattern bindings. It retains all occurrences; there is no eight-name cap. Property names, types, field labels, argument labels, and callable signatures do not become variable reads. Property/index assignments record their base as read and written; compound assignments read and write the target. Nested bodies own their own reads, while a control statement owns its header. Copy/address alias syntax is recorded without merging binding identities or inferring hidden object effects.

The concrete recognition completion in `SegmentActions.chibi` is:

1. Create maximal same-category SynS runs. Controls, returns, nested declarations such as functions and records, labels, and exits remain separate from those runs.
2. For each statement, find earlier definitions of its referenced bindings in the same sequence. Eligible preamble definitions are init, declare, or a plain assignment without a receiver. Walk those dependencies from nearest to farthest; include an earlier definition only when its range touches the current range, then reuse that definition's already computed start. This forms a contiguous dependency preamble, without skipping unrelated statements.
3. For each resolved variable binding, collect runs of occurrences in consecutive nonstructured statements of one sequence. Collapse duplicate occurrences within a statement. A run needs at least two statements. Extend its beginning with the computed preamble, including a final statement's preamble when it reaches the run's beginning.
4. Use those intervals as object-related candidates. For each control statement, use its contiguous preamble as the E-SWIFT candidate.

This uses definition/use occurrence runs, including receiver occurrences. It does not prove object identity, alias equivalence, method purity, or that the resulting group performs a single semantic action.

## Similarity functions used by phases II–IV

Split identifiers at camel-case boundaries, separators, and numbers. For word lists `a` and `b`, the published boolean rule is:

```text
ID(a,b) =
    true                                      if |a| = |b| = 1
    any(a[i] = b[i])                           if |a| = |b| > 1
    a.first = b.first or a.last = b.last        if |a|,|b| > 1 and abs(|a|-|b|) = 1
    false                                     otherwise
```

Exact equality is not a substitute for this rule: two distinct one-word names deliberately compare similar. See [§6.3.2.2, pp. 79–80](https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf#page=90). Chibi preserves case, keeps acronym runs together, splits an acronym before a following capitalized word, discards digit/separator delimiters, and treats an absent or empty word list as dissimilar. Non-ASCII identifier bytes remain in their word; case boundaries use ASCII letters.

`RHS(a,b)` is true for the same expression category. Call expressions additionally require identifier similarity. The categories include literal, variable, construction, infix, prefix, postfix, cast, direct call, and object call. Different arithmetic operators are not separate categories. See [§6.3.2.3, p. 80](https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf#page=91).

Chibi compares terminal callee names for both direct and object RHS calls, including a direct/object pair. Record, array, and map literals are constructions, as are calls to a lexically resolved local type. A call through a variable receiver is an object call; namespace members are direct calls. Unsupported or absent RHS categories compare dissimilar. Type similarity uses only an explicit declaration type's syntax name; an omitted type stays unknown rather than being guessed from the binding name or initializer.

For same-category statement pairs, let `T`, `N`, `R`, `O`, and `M` denote similarity of type, declared/assigned name, RHS, receiver, and method name. Choose the highest satisfied level:

| Category | Level 3 | Level 2 | Level 1 | Otherwise |
| --- | --- | --- | --- | --- |
| init | `T && N && R` | `T && (N || R)` | `T || R` | 0 |
| declare | unavailable | unavailable | `T` | 0 |
| assign | unavailable | `N && R` | `N || R` | 0 |
| object call | unavailable | `O && M` | `O || M` | 0 |
| direct call | unavailable | unavailable | `M` | 0 |
| other | unavailable | unavailable | unavailable | 0 |

Notably, name similarity by itself does not give an `init` pair level 1. This is a transcription of the boolean conditions in [table 6.2, p. 80](https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf#page=91), not a weighted sum.

## Phase II: remove overlap

For two consecutive candidate units sharing statement `s`, use the following cases. `O` is object-related, `E` is E-SWIFT, and `S` is SynS:

| Pair | Published decision |
| --- | --- |
| `O / O`, `s` is `init` | Keep `s` in the later unit; put the boundary before it. |
| `O / O`, other `s` | Merge the units. |
| `S / O` or `S / E` | Usually keep `s` with `O` or `E`. |
| `O / S` | Keep `s` with SynS when its adjacent SynS pair is highly similar; otherwise retain it in `O`. |
| `O / E` | Keep `s` in `O`; put the boundary after it. |

Highly similar means level at least 2 for init, assign, and object call; level 1 for declare and direct call. See [§6.3.2.4, pp. 81–83](https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf#page=92).

The section's introductory example, listing 6.1, needs the high-similarity exception for a preceding SynS unit. The detailed case is titled only `O / S`, but its prose also mentions E-SWIFT. Reversed pairs, containment, and three-way conflicts have no explicit ordering in the recovered text.

Chibi sorts object intervals by source start, longer first at equal starts. It discards duplicate or contained intervals. For crossing intervals, an init at the later start belongs to the later unit; another category merges the intervals. It then assigns object membership before E-SWIFT membership. An E-SWIFT preamble stops after its nearest object-owned statement. Finally, the high-similarity SynS exception applies at a proper crossing with either endpoint of an object or control candidate. A SynS interval that contains an entire action does not peel off that action's endpoints. This keeps separate complete object actions intact while allowing the published boundary-overlap cases. Remaining runs with the same owner become the nonoverlapping parts. These are deterministic completions, not recovered Java-source behavior.

## Phase III: refine SynS

For each surviving SynS unit with at least four statements, use its original ordered sequence for a sliding scan:

```text
for each consecutive triple a,b,c:
    x = pair_similarity(a,b)
    y = pair_similarity(b,c)
    if x < y: insert boundary between a and b
    if y < x: insert boundary between b and c
```

Equal levels add no boundary. Scan every original triple; do not restart the window at inserted boundaries. Duplicate boundary requests collapse to one. No operation is required for units of one to three statements. See [§6.3.3, pp. 83–84](https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf#page=94).

The literal names in listing 6.9 expose another inconsistency. The stated levels for `in.start`, `out.start`, `error.start`, `out.join`, `error.join` require `start` and `join` to differ, but the one-word ID rule makes them similar. Preserve the published ID rule and explain any compound-name translation used to test the intended window example. Do not change the ID rule to force that example's result.

## Phase IV: merge singleton units

Only singleton units are targeted. Its cross-category compatibility table is:

| Neighboring categories | Merge feature |
| --- | --- |
| init / declare | Similar type |
| init / assign | Similar RHS or variable name |
| direct call / object call | Similar method name or parameter |

See [§6.3.4 and table 6.3, pp. 84–85](https://udspace.udel.edu/bitstream/handle/19716/21806/Wang_udel_0060D_12922.pdf#page=95). Listing 6.11 merges three alternating init/assign statements because their constructions have similar RHS categories.

The text does not specify traversal, ties, repeated passes, or parameter-list similarity. Chibi makes one left-to-right pass. A singleton first tries the previously emitted block, then the following block. Compatibility compares their adjacent boundary statements. Following-block merges consume that block immediately; previous-block merges extend the existing result. Only object/SynS parts can merge, using the cross-category table above. Controls, returns, isolated statements, and hard boundaries are ineligible.

The singleton test counts one AST statement rather than one physical line, so wrapping does not change eligibility. Parameter similarity requires nonempty equal-arity lists. Every corresponding argument must have the same supported syntax category; identifiers and calls also require ID similarity of their name or terminal callee name. Parentheses, `await`, named argument labels, and typed argument wrappers are unwrapped first. Literal values are compared by category, not by value. These choices complete the undefined parameter rule without adding tuned scores.

## Acceptance evidence

Keep direct tests for the published decisions: listing 6.2 cuts before statement 3; 6.4 before statement 2; 6.6 before statement 3; 6.8 after statement 3; 6.9's intended window behavior before statement 4; and 6.11 removes the singleton boundaries. Test the literal identifier rule separately from the inconsistent listing 6.9 names. Test three-versus-four-statement recursion and SynS refinement thresholds separately.

Formatter validation must also check repeated formatting, preservation of statement tokens and attached comments, protected bytes, and behavior under wrapping and terminator options. Authored separators are preserved constraints, so removing them can change eligible windows. Test outcomes belong in the [implementation record](formatter-implementation.md); the published algorithm alone does not establish the Chibi formatter's fixed point.
