// Mode `fan-out` (DESIGN §8.5): 1 sender thread x K actors x N/K messages.
// Ready-queue stress: every send may wake a different actor, so the ready
// stacks and the worker pool, not one mailbox, are the bottleneck.
//
// Self-reporting: each actor is asked for its state after its sends, and the
// asks are ordered behind them, so the sum of the replies is exactly N.
val e = System.getenv("PROTOSCALA_BENCH_N")
val N = if e == "" then 1000000 else e.toInt
val K = 1000
val per = N / K
var actors: List[Any] = Nil
var k = 0
while k < K do
  actors = Actor.spawn(0) { (s, m) => (s + 1, s + 1) } :: actors
  k += 1
var round = 0
while round < per do
  actors.foreach(a => a ! 1)
  round += 1
var processed = 0
actors.foreach(a => processed += (a ? 1).await - 1)
println("mode=fan-out messages=" + (K * per + K).toString + " processed=" + processed.toString)
println(Actor.stats)
println(if processed == K * per then "ok" else "FAILED")
