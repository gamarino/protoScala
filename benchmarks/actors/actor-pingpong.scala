// Mode `ping-pong` (DESIGN §8.5): ask/reply latency measured *through* the
// cooperative suspension. `ping`'s handler awaits an ask to `pong` R times, so
// every round trip is two messages and one frame snapshot plus one resume.
//
// Self-reporting: `pong` replies 1 to every message, so the accumulator `ping`
// returns is exactly R.
val e = System.getenv("PROTOSCALA_BENCH_N")
val N = if e == "" then 100000 else e.toInt
val R = N / 2
val pong = Actor.spawn(0) { (s, m) => (s + 1, 1) }
val ping = Actor.spawn(0) { (s, m) =>
  var acc = 0
  var i = 0
  while i < R do
    acc += (pong ? 1).await
    i += 1
  (acc, acc)
}
val processed = (ping ? 0).await
println("mode=ping-pong messages=" + (R + 1).toString + " processed=" + processed.toString)
println(Actor.stats)
println(if processed == R then "ok" else "FAILED")
