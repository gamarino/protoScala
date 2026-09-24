// EXPECT-ERROR: expects Priority.High, Priority.Medium or Priority.Low
// Late detection, never silent failure: the message names what was expected and
// what arrived.
@main def run(): Unit =
  val a = Actor.spawn(0) { (s, m) => (s, m) }
  a.send("x", "urgent")
  println(a.value)
