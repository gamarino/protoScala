// EXPECT: mapped-through IllegalStateException recovered
// `map` and `flatMap` forward the exception value unchanged; `recover` receives it
// and may pattern-match on it, as a `catch` clause would.
@main def run(): Unit =
  val failer = Actor.spawn(0) { (s, m) => throw new IllegalStateException("boom") }
  val mapped = (failer ? 1).map(v => v.toString + "!")
  while !mapped.isCompleted do ()
  val name = mapped.value.get match
    case Failure(e) => e.getClass
    case Success(v) => "unexpected " + v
  val recovered = (failer ? 1).recover(e => "recovered")
  println("mapped-through " + name + " " + recovered.await)
