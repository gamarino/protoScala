// EXPECT: echo a=1 b=2
// A NAMED ARGUMENT ACROSS A REAL UMD BOUNDARY. The callee reads it out of
// protoCore's keywordParameters, keyed by the ADDRESS of the interned
// parameter-name symbol, with no adapter on either side (INTEROP §7).
import js.probe as p
@main def run(): Unit = println(p.echo(1, b = 2))
