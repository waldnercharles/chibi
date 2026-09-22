These fixtures measure **asset-type selection inside an explicitly selected
create_asset workflow**, not general intent recognition or GUI planning.
Development prompts were used while designing the request. Held-out prompts
and challenge cases are evaluated unchanged. Each menu is also reversed to
measure position sensitivity. The unsupported/ambiguous cases require an
abstention (confidence below the fixed 0.8 threshold).

Compare the previous generic next-target question and compact OCR-like state
against a single asset-type question and the same menu labels. This isolates
question/state formulation; it does not claim to benchmark OCR recognition or
end-to-end GUI speed. Timings include localhost HTTP model inference. The
service's confidence is normalized entropy, not a calibrated success rate.
