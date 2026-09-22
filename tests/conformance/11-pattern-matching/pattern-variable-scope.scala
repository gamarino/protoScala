// EXPECT: 1 100 exact
// Regression guard for the CaptureAnalysis scoping of Match cases: without it
// a case body's names resolve to the enclosing block instead of the pattern.
@main def run(): Unit =
  // `q` is the pattern's variable, not the `val q` defined later in the
  // block; resolving it to the latter reports a spurious forward reference.
  val r = List(1) match
    case q :: Nil => q
    case _ => 0
  val q = 9
  // A closure over a pattern variable sees the bound value, not the outer
  // `val n` of the same name.
  val n = 100
  val s = List(7) match
    case n :: Nil => { val f = () => n * 100; f() / n }
    case _ => 0
  // A stable identifier in a pattern reads a local of an enclosing function:
  // `check` is hoisted, so `target` must be boxed for the closure to see the
  // value its initialiser stores.
  val target = 7
  def check(v: Int) = v match
    case `target` => "exact"
    case _ => "other"
  println(r.toString + " " + s + " " + check(7))
