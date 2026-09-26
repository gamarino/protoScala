// EXPECT: 1
// dotty decides the statement separator before it looks at the indentation
// width, so a blank line ends the statement however deeply the next line is
// indented. scalac 3.9 prints 1 here (with "A pure expression does nothing in
// statement position" for the orphaned `+ a * 6`); without the blank line the
// same program prints 31.
@main def run(): Unit =
  val a = 5
  val x = 1

    + a * 6
  println(x)
