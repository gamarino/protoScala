// EXPECT: 25.0
// Tutorial chapter 17.3. The chapter shows protoscalac REFUSING this program
// (D118: a class is not transpiled in the first cut), and the point of the fixture
// is that the same program is perfectly good protoScala: it runs on the
// interpreter and prints its answer.
//
// The refusal itself is checked by tests/cli/transpiler-refusals.sh, which asserts
// the message, the line number and that no .cpp is left behind. This fixture is on
// tests/transpile-exclude.txt under D118, and the harness checks that list in BOTH
// directions -- so on the day classes are transpiled, this fixture fails and asks
// to be taken off the list.
class Square(val side: Double):
  def area: Double = side * side

@main def run(): Unit =
  println(new Square(5.0).area)
