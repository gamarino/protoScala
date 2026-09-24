// EXPECT: ok 3
// The integer surface of D52 still works: 0, 1 and 2 are the High, Medium and Low
// bands, so a program written before `Priority` became an `enum` keeps running.
// The order in which the bands drain is `priority-bands.scala`'s business; this
// fixture only pins that an Int is accepted, which is deterministic.
@main def run(): Unit =
  val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
  a.ask("x", 0).await
  a.ask("y", 1).await
  val n = a.ask("z", 2).await
  println("ok " + n)
