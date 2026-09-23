// Mode `saturation-8`: 8 actors x N/8 CPU-bound messages, one sender thread.
//
// Why this mode exists. The other six modes cannot exhibit a rise up to the
// machine's physical core count, because each is capped by its own structural
// concurrency and not by the cores: `single` has one producer and one actor,
// `fan-out` one producer, `MPSC` four producers against one actor, and `MPMC`
// four producers against four actors (and duly peaks at w=4). `fan-out` is
// worse than uninformative here — it is producer-bound, with 78-99% of its
// measurable window inside the sender's own loop, so adding workers cannot
// help it (see benchmarks/reports/2026-09-23-actors-v4-curve.md).
//
// This mode removes the producer from the critical path instead of trying to
// make it faster. Every message carries real compute — a 20,000-iteration
// summation, roughly 1.5 ms of interpreter work — so the send loop costs well
// under 1% of the run and the workers, not the sender, are the constraint.
// With 8 actors the single-method invariant allows at most 8 to run at once,
// so the curve should rise with workers to the physical core count and then
// pay for oversubscription. This mirrors protoST's `saturation_8a.st`, where
// N_actors == N_workers leaves no work-stealing opportunity.
//
// Self-reporting: `processed` is the **sum the actors actually computed**, not
// a message count, so a handler that silently did no work cannot pass. Each
// message adds sum(1..20000) = 200,010,000 to its actor's state; the closing
// ask is itself a message and adds one more, so the total over K actors is
// (N + K) * 200,010,000. The asks are ordered behind the sends by the
// single-method invariant, so every prior message is already folded in.
val e = System.getenv("PROTOSCALA_BENCH_N")
val N = if e == "" then 4800 else e.toInt
val K = 8
val per = N / K
val W = 20000
val PERMSG = 200010000 // sum(1..W)
var actors: List[Any] = Nil
var k = 0
while k < K do
  actors = Actor.spawn(0) { (s, m) =>
    var sum = 0
    var i = 1
    while i <= W do
      sum = sum + i
      i += 1
    (s + sum, s + sum)
  } :: actors
  k += 1
var round = 0
while round < per do
  actors.foreach(a => a ! 1)
  round += 1
var processed = 0
actors.foreach(a => processed += (a ? 1).await)
val messages = K * per + K
println("mode=saturation-8 messages=" + messages.toString + " processed=" + processed.toString)
println(Actor.stats)
println(if processed == messages * PERMSG then "ok" else "FAILED")
