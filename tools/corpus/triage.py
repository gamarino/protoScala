#!/usr/bin/env python3
"""Triage the Scala 3 (dotty) `tests/run` corpus into three buckets.

One reason per test, and no wildcard exclusion: every rule names the construct it
found, and the rule that fired is recorded per test in the output. Nothing is
excluded from the measurement run -- every corpus file is run regardless of bucket;
the buckets only attribute the outcome.

  bucket 1  the test needs the JVM, Java interop or reflection
  bucket 2  the test needs a Scala construct protoScala does not claim to have
  bucket 3  no rule found an unsupported construct -- THE IN-SCOPE SET

Read tools/corpus/README.md before quoting a bucket-3 rate: the rules hunt for
known-missing constructs, so bucket 3 is a residue and the bias in it has a known
direction.
"""
import argparse
import collections
import json
import os
import re
import sys

# (bucket, rule-name, regex over source) -- ordered; first match wins
SRC_RULES = [
    (1, "classOf", r"\bclassOf\b"),
    (1, "Java interop (java./javax. reference)", r"\b(?:java|javax)\.[a-z]"),
    (1, "JVM reflection via getClass", r"\.getClass\b"),
    (1, "synchronized", r"\bsynchronized\b"),
    (1, "Java threads / j.u.concurrent", r"\bnew\s+Thread\b|Thread\.currentThread|java\.util\.concurrent"),
    (1, "serialization", r"\bSerializable\b|ObjectOutputStream|ObjectInputStream|writeObject|readResolve|writeReplace"),
    (1, "reflection / ClassTag / Mirror / TypeTest",
     r"scala\.reflect|\bClassTag\b|\bTypeTest\b|\bManifest\b|deriving\.Mirror|\bMirror\b|scala\.quoted|reflect\.Selectable|Selectable\b"),
    (1, "JVM System / Console / Predef JVM surface", r"\bSystem\.(?:out|err|in|exit|getProperty|arraycopy|identityHashCode)|\bConsole\b"),
    (1, "scala.compiletime / staging", r"scala\.compiletime|\bstaging\b"),

    (2, "implicit / given / using (D3)", r"(?m)^\s*(?:implicit|given)\b|\busing\b|\bimplicitly\b|\bsummon\b|\(implicit\b"),
    (2, "inline / macro / quotes", r"\binline\b|\bmacro\b|\bExpr\[|\bQuotes\b|'\{|\$\{"),
    (2, "PartialFunction / collect (D63)", r"\bPartialFunction\b|\.collect\b|\.collectFirst\b|\borElse\b"),
    (2, "lazy collection / Iterator / view (D100)",
     r"\bLazyList\b|\bIterator\b|\bIterable\b|\.view\b|\blazyZip\b|\bStream\b|#::"),
    (2, "anonymous class or class nested in a class (D80)",
     r"\bnew\s+[A-Za-z_][\w.]*(?:\[[^\]]*\])?(?:\([^)]*\))?\s*\{|\bnew\s*\{"),
    (2, "Seq / mutable collections / ArrayBuffer (D65)",
     r"\bSeq\b|\bArrayBuffer\b|\bListBuffer\b|\bmutable\.|\bHashMap\b|\bSortedMap\b|\bListMap\b|\bTreeMap\b"),
    (2, "Array (D69 / no Array type)", r"\bArray\b|\bIArray\b"),
    (2, "export clause", r"(?m)^\s*export\b"),
    (2, "type-level feature (opaque / match type / higher-kinded bound)",
     r"\bopaque\s+type\b|\bmatch\s+type\b|\berased\b|\btransparent\b"),
    (2, "regular expressions (D70)", r"\.r\b|\bRegex\b|\bMatching\b"),
    (2, "Ordering / Numeric (D62)", r"\bOrdering\b|\bNumeric\b|\bIntegral\b"),
]

# rules over the .check file: the expectation itself is JVM-specific
CHK_RULES = [
    (1, "checkfile expects a JVM-qualified class name", r"\bjava\.(?:lang|io|util|nio)\."),
    (1, "checkfile expects a JVM stack trace", r"(?m)^\s+at \S+\(|\.scala:\d+\)\s*$"),
    (1, "checkfile expects a scala.* qualified name", r"\bscala\.[A-Za-z]"),
    (1, "checkfile expects a JVM collection name", r"\b(?:ArraySeq|WrappedArray|Array)\("),
]

MAIN_SIG = re.compile(r"def\s+main\s*\(\s*\w+\s*:\s*Array\s*\[\s*String\s*\]\s*\)")


def strip_noise(src):
    """Remove comments and the main signature so they cannot trip a rule."""
    src = re.sub(r"//[^\n]*", "", src)
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    src = MAIN_SIG.sub("def main(args)", src)
    return src


def triage(corpus, name):
    with open(os.path.join(corpus, name + ".scala"), encoding="utf-8",
              errors="replace") as f:
        src = f.read()
    s = strip_noise(src)
    for bucket, rule, rx in SRC_RULES:
        if re.search(rx, s):
            return bucket, rule
    ckp = os.path.join(corpus, name + ".check")
    if os.path.exists(ckp):
        with open(ckp, encoding="utf-8", errors="replace") as f:
            ck = f.read()
        for bucket, rule, rx in CHK_RULES:
            if re.search(rx, ck):
                return bucket, rule
    return 3, "no out-of-scope construct found"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--corpus", default=os.environ.get("SCALA3_CORPUS"),
                    help="dotty checkout's tests/run directory (default: $SCALA3_CORPUS)")
    ap.add_argument("--out", default="triage.jsonl", help="triage file to write")
    ap.add_argument("--results", default=None,
                    help="optional results.jsonl, to print pass rates per bucket")
    a = ap.parse_args(argv)
    if not a.corpus or not os.path.isdir(a.corpus):
        ap.error("no corpus: pass --corpus or set SCALA3_CORPUS "
                 "(tools/corpus/README.md has the clone command)")
    names = sorted(os.path.splitext(f)[0] for f in os.listdir(a.corpus)
                   if f.endswith(".scala"))
    res = {}
    if a.results:
        with open(a.results) as f:
            for line in f:
                r = json.loads(line)
                res[r["test"]] = r
    bc = collections.Counter()
    bs = collections.Counter()
    rule_counts = collections.Counter()
    with open(a.out, "w") as out:
        for name in names:
            b, rule = triage(a.corpus, name)
            bc[b] += 1
            rule_counts[(b, rule)] += 1
            rec = {"test": name, "bucket": b, "reason": rule}
            if name in res:
                rec["status"] = res[name]["status"]
                bs[(b, res[name]["status"])] += 1
            out.write(json.dumps(rec) + "\n")
    print("bucket sizes:", dict(sorted(bc.items())))
    if res:
        for b in (1, 2, 3):
            tot = bc[b]
            p = bs[(b, "pass")]
            print(f"bucket {b}: n={tot} pass={p} fail={bs[(b, 'fail')]} "
                  f"timeout={bs[(b, 'timeout')]} "
                  f"rate={100.0 * p / tot if tot else 0:.1f}%")
    print("\nrules fired:")
    for (b, r), n in rule_counts.most_common():
        print(f"  b{b} {n:5d}  {r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
