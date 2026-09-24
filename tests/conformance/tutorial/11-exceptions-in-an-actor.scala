// EXPECT: IllegalStateException the actor is alive
// A handler exception fails that one message and leaves the actor alive with its
// previous state; the ask's future carries the exception value itself.
@main def run(): Unit =
  val counter = Actor.spawn(0) { (s, m) =>
    if m == 0 then throw new IllegalStateException("cannot count zero") else (s + m, s + m)
  }
  val bad = counter ? 0
  while !bad.isCompleted do ()
  val name = bad.value.get match
    case Failure(e) => e.getClass
    case Success(v) => "unexpected"
  val alive = (counter ? 5).await
  println(name + " " + (if alive == 5 then "the actor is alive" else "lost"))
