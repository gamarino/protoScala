// Mode `MPSC` (DESIGN §8.5): P producer threads x N/P messages -> 1 actor.
// Sender contention on one mailbox. The producers are real OS threads
// (D49), never workers: a producer running on the pool would deadlock the
// workers=1 row.
//
// Self-reporting: the producers are joined before the ask, so the reply is
// exactly the number of messages the actor processed.
val e = System.getenv("PROTOSCALA_BENCH_N")
val N = if e == "" then 1000000 else e.toInt
val P = 4
val per = N / P
val sink = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
val ts = List(1, 2, 3, 4).map { _ =>
  Thread.start { () =>
    var i = 0
    while i < per do
      sink ! 1
      i += 1
  }
}
ts.foreach(t => t.join())
val processed = (sink ? 1).await - 1
println("mode=MPSC messages=" + (P * per + 1).toString + " processed=" + processed.toString)
println(Actor.stats)
println(if processed == P * per then "ok" else "FAILED")
