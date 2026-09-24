// EXPECT: 42
// Point 4.
@main def run(): Unit =
  val counter = Actor.spawn(0)((state, msg) => (state + msg, state + msg))
  counter ! 5
  println((counter ? 0).await)
  println(Future(6 * 7).await)
