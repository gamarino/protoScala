// Mode `single` (DESIGN §8.5): 1 sender thread x 1 actor x N trivial messages.
// The per-actor pipeline floor: the single-method invariant serialises one
// actor, so more workers cannot help.
//
// Self-reporting: the final ask is ordered behind every send on the same band,
// so its reply is the exact number of messages this actor processed, N + 1.
val e = System.getenv("PROTOSCALA_BENCH_N")
val N = if e == "" then 1000000 else e.toInt
val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
var i = 0
while i < N do
  a ! 1
  i += 1
val processed = (a ? 1).await
println("mode=single messages=" + (N + 1).toString + " processed=" + processed.toString)
println(Actor.stats)
println(if processed == N + 1 then "ok" else "FAILED")
