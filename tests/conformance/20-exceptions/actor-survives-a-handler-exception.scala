// EXPECT: 12
// A handler exception fails that one message and leaves the actor alive with its
// previous state (DESIGN §8.4), now with a real exception value.
@main def run(): Unit =
  val a = Actor.spawn(0) { (s, m) =>
    if m == 0 then throw new RuntimeException("bad") else (s + 1, s + 1)
  }
  val bad = a ? 0
  while !bad.isCompleted do ()
  println((a ? 1).await.toString + (a ? 1).await.toString)
