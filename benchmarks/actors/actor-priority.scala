// Mode `priority` (DESIGN §8.5): a Low-band flood of N messages plus S
// High-band asks, each timed with System.nanoTime (D49). The runner turns the
// printed samples into p50/p99 and compares them with the Low-band flood's
// mean service time.
//
// Self-reporting: the final Low-band ask is ordered behind the flood, so its
// reply is the exact number of Low-band messages the actor processed.
val e = System.getenv("PROTOSCALA_BENCH_N")
val N = if e == "" then 200000 else e.toInt
val S = 1000
val every = N / S
val sink = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
val t0 = System.nanoTime()
var samples = ""
var i = 0
while i < N do
  sink.send(1, Priority.Low)
  if i % every == 0 then
    val start = System.nanoTime()
    val r = sink.ask(1, Priority.High).await
    samples = samples + (System.nanoTime() - start).toString + " "
  i += 1
val floodEnd = System.nanoTime()
val processed = sink.ask(1, Priority.Low).await
println("mode=priority messages=" + (N + S + 1).toString + " processed=" + processed.toString)
println("flood_ns=" + (floodEnd - t0).toString + " flood_messages=" + (N + S).toString)
println("latencies=" + samples.trim)
println(Actor.stats)
println(if processed == N + S + 1 then "ok" else "FAILED")
