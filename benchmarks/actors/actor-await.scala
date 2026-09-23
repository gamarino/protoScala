// Mode `await` (DESIGN §8.5): C caller actors, each awaiting an ask to one
// shared `echo` actor R times. It MUST complete with PROTOSCALA_ACTOR_WORKERS=1,
// which a blocking await never could: the worker is released at every
// suspension, so a single worker runs the callers and the echo in turn.
//
// Self-reporting: each caller returns the number of replies it awaited, so the
// sum of the replies is exactly C * R.
val e = System.getenv("PROTOSCALA_BENCH_N")
val N = if e == "" then 100000 else e.toInt
val C = 100
val R = N / (2 * C)
val echo = Actor.spawn(0) { (s, m) => (s + 1, 1) }
var callers: List[Any] = Nil
var k = 0
while k < C do
  callers = Actor.spawn(0) { (s, m) =>
    var acc = 0
    var i = 0
    while i < R do
      acc += (echo ? 1).await
      i += 1
    (acc, acc)
  } :: callers
  k += 1
var futures: List[Any] = Nil
callers.foreach(c => futures = (c ? 0) :: futures)
var processed = 0
futures.foreach(f => processed += f.await)
println("mode=await messages=" + (C * R + C).toString + " processed=" + processed.toString)
println(Actor.stats)
println(if processed == C * R then "ok" else "FAILED")
