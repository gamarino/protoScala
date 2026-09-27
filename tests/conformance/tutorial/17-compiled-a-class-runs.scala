// EXPECT: 25.0
// Tutorial chapter 17.3. The chapter used to show protoscalac REFUSING this program
// (D118: the first cut did not transpile a class). D118 closed on 2026-09-27, so the
// same file now demonstrates the opposite: it runs on the interpreter AND transpiles,
// and the differential harness runs it down both paths.
//
// It is kept, renamed rather than deleted, because the way it changed hands is the
// lesson: it was on tests/transpile-exclude.txt under D118, and the harness checks
// that list in BOTH directions -- so the day classes were transpiled, this fixture
// failed and asked to be taken off the list. It was.
class Square(val side: Double):
  def area: Double = side * side

@main def run(): Unit =
  println(new Square(5.0).area)
