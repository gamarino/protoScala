// EXPECT: echo a=1 b=2
// The same as stand-in-provider-keyword.scala, with a parameter name too long to
// embed in a pointer word. A key built with fromUTF8String instead of
// createSymbol works by accident for `b` and fails silently here, which is why
// the convention names createSymbol.
//
// STILL LOAD-BEARING AFTER protoCore 2.2.0 (P3), and verified so rather than
// assumed. P3 made interning process-global, so a name is now one pointer in
// every ProtoSpace — but this fixture never tested that. It tests the
// createSymbol-versus-fromUTF8String convention WITHIN one space
// (tests/unit/probe_provider.cpp, keywordNamed): fromUTF8String returns an
// UNINTERNED string, whose address is a fresh one every call, so it can only
// match a keywordParameters key by the pointer-word embedding accident.
// Re-measured by mutation on 2026-09-25: with keywordNamed switched to
// fromUTF8String, stand-in-provider-keyword.scala (1-byte `b`) still PASSES and
// this fixture FAILS with `echo a=1`. That asymmetry is the trap, and P3 does not
// remove it.
import js.probe as p
@main def run(): Unit = println(p.echo(1, aVeryLongKeywordName = 2))
