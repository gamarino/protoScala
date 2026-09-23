// Mode `MPMC` (DESIGN §8.5): P producer threads x C actors, round-robin.
// Contention on several mailboxes at once, which is where the per-actor claim
// and the three ready stacks have to scale.
//
// Self-reporting: every producer is joined before the asks, so the sum of the
// replies is exactly the number of messages the actors processed.
val e = System.getenv("PROTOSCALA_BENCH_N")
val N = if e == "" then 1000000 else e.toInt
val P = 4
val per = N / P
val a0 = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
val a1 = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
val a2 = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
val a3 = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
val actors = List(a0, a1, a2, a3)
val ts = List(1, 2, 3, 4).map { _ =>
  Thread.start { () =>
    var i = 0
    while i < per do
      a0 ! 1
      a1 ! 1
      a2 ! 1
      a3 ! 1
      i += 4
  }
}
ts.foreach(t => t.join())
var processed = 0
actors.foreach(a => processed += (a ? 1).await - 1)
println("mode=MPMC messages=" + (P * per + 4).toString + " processed=" + processed.toString)
println(Actor.stats)
println(if processed == P * per then "ok" else "FAILED")
