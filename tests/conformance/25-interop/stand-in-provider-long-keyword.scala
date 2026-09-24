// EXPECT: echo a=1 b=2
// The same, with a parameter name too long to embed in a pointer word. A key
// built with fromUTF8String instead of createSymbol works by accident for `b`
// and fails silently here, which is why the convention names createSymbol.
import js.probe as p
@main def run(): Unit = println(p.echo(1, aVeryLongKeywordName = 2))
