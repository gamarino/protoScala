#!/usr/bin/env python3
"""Regenerates or checks the bytecode examples of docs/VM.md.

    python3 -I docs/vm-examples/disasm_examples.py PROTOSCALA [--check | --update]

Every docs/vm-examples/NN-name.scala is compiled with
`PROTOSCALA --disassemble NN-name.scala`; its listing is the golden file
NN-name.txt next to it. docs/VM.md embeds each example after the marker line
`<!-- vm-example: NN-name -->` as a ```scala block (the source) followed by a
```text block (the listing).

--check (the default, and the ctest `docs/vm-examples`) fails when
  - an example does not run (`PROTOSCALA NN-name.scala` exits non-zero), so a
    listing in the documentation is always the bytecode of a working program;
  - a listing differs from its golden file;
  - VM.md's copy of a source or a listing differs from the files;
  - an example is missing from VM.md.
A compiler change that changes the bytecode therefore cannot leave the
documentation behind silently.

--update rewrites the golden files and VM.md's copies from the current
protoscala. Review the diff: the prose around each example explains the
listing instruction by instruction.

The examples are compiled with PROTOSCALA_FIELD_GROUPS removed from the
environment, so the constructor field stores take their default form.

Exit status 0 when everything matches (or after an update).
"""
import os, pathlib, re, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent
VM_MD = HERE.parent / "VM.md"
MARKER = re.compile(r"^<!-- vm-example: (\S+) -->$")


def environment():
    env = dict(os.environ)
    env.pop("PROTOSCALA_FIELD_GROUPS", None)
    return env


def run(protoscala, args):
    return subprocess.run([protoscala] + args, cwd=HERE, capture_output=True, text=True,
                          timeout=120, env=environment())


def listing(protoscala, source):
    r = run(protoscala, ["--disassemble", source.name])
    if r.returncode != 0:
        sys.exit(f"FAIL {source.name}: protoscala --disassemble exited {r.returncode}: "
                 f"{r.stderr.strip()}")
    return r.stdout.replace("\r\n", "\n")


def runs(protoscala, source):
    """None when the example runs and exits 0, else a description of the failure."""
    r = run(protoscala, [source.name])
    if r.returncode == 0:
        return None
    return f"exited {r.returncode}: {(r.stderr or r.stdout).strip()}"


def embed(md, name, source_text, listing_text):
    """VM.md with example `name` replaced; None when its marker is missing."""
    lines = md.split("\n")
    for i, line in enumerate(lines):
        m = MARKER.match(line)
        if not m or m.group(1) != name:
            continue
        # The marker is followed by a ```scala block and a ```text block, with
        # only blank lines between them.
        j = i + 1
        blocks = []
        while len(blocks) < 2:
            while j < len(lines) and lines[j].strip() == "":
                j += 1
            if j >= len(lines) or not lines[j].startswith("```"):
                sys.exit(f"FAIL VM.md: example {name} must be followed by a ```scala "
                         "and a ```text block")
            start = j
            j += 1
            while j < len(lines) and lines[j] != "```":
                j += 1
            blocks.append((start, j))
            j += 1
        new = (["```scala"] + source_text.rstrip("\n").split("\n") + ["```", ""] +
               ["```text"] + listing_text.rstrip("\n").split("\n") + ["```"])
        return "\n".join(lines[:i + 1] + new + lines[blocks[1][1] + 1:])
    return None


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    protoscala = str(pathlib.Path(sys.argv[1]).resolve())
    update = "--update" in sys.argv[2:]
    md = VM_MD.read_text(encoding="utf-8").replace("\r\n", "\n")
    original_md = md
    failed = 0
    sources = sorted(HERE.glob("*.scala"))
    if not sources:
        sys.exit("FAIL: no examples in docs/vm-examples")
    for source in sources:
        name = source.stem
        problem = runs(protoscala, source)
        if problem:
            failed += 1
            print(f"FAIL {name}: the example does not run: {problem}")
            continue
        source_text = source.read_text(encoding="utf-8").replace("\r\n", "\n")
        out = listing(protoscala, source)
        golden = source.with_suffix(".txt")
        if update:
            golden.write_text(out, encoding="utf-8", newline="\n")
        elif not golden.exists() or golden.read_text(encoding="utf-8").replace("\r\n", "\n") != out:
            failed += 1
            print(f"FAIL {name}: the listing differs from {golden.name}")
            print(out)
            continue
        new_md = embed(md, name, source_text, out)
        if new_md is None:
            failed += 1
            print(f"FAIL {name}: VM.md has no `<!-- vm-example: {name} -->` marker")
            continue
        if not update and new_md != md:
            failed += 1
            print(f"FAIL {name}: VM.md's copy differs from {source.name} / {golden.name}")
        md = new_md
        print(f"ok   {name}")
    if update and md != original_md:
        VM_MD.write_text(md, encoding="utf-8", newline="\n")
        print("updated docs/VM.md")
    if failed:
        print(f"{failed} example(s) out of date: run with --update and review the diff")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
