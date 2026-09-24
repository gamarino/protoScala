// EXPECT: IllegalStateException boom true
// Since Phase 4 the ask's future carries the exception VALUE, not a description of
// it, so the caller can pattern-match on the class and read its message (D44
// retired).
@main def run(): Unit =
  val failer = Actor.spawn(0) { (s, m) => throw new IllegalStateException("boom") }
  val f = failer ? 1
  while !f.isCompleted do ()
  val t = f.value.get
  val out = t match
    case Failure(e) => e.getClass + " " + e.getMessage + " " + t.isFailure
    case Success(v) => "unexpected " + v
  println(out)
