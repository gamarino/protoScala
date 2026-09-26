# Mutation patches for the transpiler (Phase 7 Task 12)

**Every differential case must be shown to fail under a named mutation of the
generator.** A code-generator suite that has never been red is a suite whose
coverage is unknown, and this directory is what makes that checkable.

`apply.py` holds the mutations as (file, exact-old-text, new-text) triples and
applies exactly one at a time against a pristine backup:

```bash
python3 tests/mutations/apply.py backup      # once, from a clean tree
python3 tests/mutations/apply.py E1          # apply one
cmake --build build_release -j3
ctest --test-dir build_release -R '^transpiled/|unit.*exceptions|ForeignBoundary' -j3 < /dev/null
python3 tests/mutations/apply.py restore     # back to pristine
```

An anchor that does not appear **exactly once** is a hard error, so a mutation
cannot silently become a no-op when the code it targets moves.

**One trap, which caught this harness the first time it was run.** `restore` uses
`copyfile` plus an explicit `touch`, **never `copy2`**: `copy2` preserves the
backup's mtime, so a restored file is older than the object `make` already produced,
`make` does not rebuild it, and the previous mutation's object file survives into the
next measurement. The symptom is several rows reporting the same suspiciously round
number. The measured matrix, the correction and the four of the plan's mutations that
cannot be applied to this cut are in
`../../.agent_scratch/phase7-transpiler/mutations-emitter.md`.
