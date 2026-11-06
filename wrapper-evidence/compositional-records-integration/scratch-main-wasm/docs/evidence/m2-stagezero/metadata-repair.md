# Two remaining d459 differential failures

Source d4593a168d6b4adb3eb51d199d43d50a764170b7, tree66a8e762008359210ad964ecf75483fb43d53b4e. Raw logs are under d459/.

`language-package-compiler-chibi-types`, sample `untyped constant expressions`:

```chibi
package facts
Count: uint : 3;
Eight :: (x: int64) -> bool { return x == (1 << Count); }
Huge :: (f: double) -> bool { return f / 0 > 1000000.0; }
```

Independent fixture results are Eight(8)==true and Huge(1)==true. The failure
is metadata: stage one lacks stage zero's expression point kind2 at bytes74..84
(the inner `1 << Count`). Folding the outer parentheses bypasses the existing
inner expression visit. The compiled execution selector passes.

`language-package-compiler-chibi`, sample `wrapped literal in parentheses`:

```chibi
package facts
One :: () -> int { return int(wrap: (0xFFFFFFFFFFFFFFFF)); }
```

The literal expected result is -1. Stage zero has one watch at40..71, while
stage one has two otherwise identical watches. Both FoldStep and Body add it.
The compiled execution selector passes.

The bounded proposed repair keeps ordinary ValueStep parenthesis traversal,
which forwards contextual storage/container/record unchanged. FoldStep retains
its watch only for NumberLiteral-shaped expressions (notably unary~0) that Body
excludes from its generic watch. No shared Folds/NumberLiteral policy change,
new evaluator, global suppression or runtime evaluation scheduling is involved.
The added direct/grouped~0 fixture pins -1 and compares reference metadata;
a production mutation removing the narrow watch must make this case fail.
Independent patch review and fresh execution are required before acceptance.

Independent read-only review approved the revised exact three-file delta:
parenthesis traversal preserves contextual typing and visits once, ordinary
folded expressions use Body's watch, and literal-shaped unary expressions retain
one FoldStep watch. Shared classification/evaluation/effects are unchanged.
Execution remains pending at this source checkpoint; the negative control must
prepare successfully and fail specifically on the new sample's missing watch.
