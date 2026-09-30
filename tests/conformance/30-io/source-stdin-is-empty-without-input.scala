// EXPECT: 0 null
// With nothing on standard input (the fixture runner gives the program none),
// Source.stdin has no lines and StdIn.readLine() answers null, as Scala's does
// at the end of input. tests/cli/io-stdin.sh feeds real input.
println(Source.stdin.getLines().length.toString + " " + StdIn.readLine())
