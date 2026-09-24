// EXPECT: ok 400
// The in-flight exception value must stay reachable for the collector from the
// throw until the handler reads it (DESIGN §7, P1). 400 throws, each unwinding 60
// frames that have each already returned a value, with allocation throughout, so
// the collector is working while a payload is in flight.
//
// What this fixture proves and what it does not: it fails if a payload is ever
// lost between the throw and `e.getMessage`, and it is green. It does NOT
// isolate the per-frame re-rooting in ProtoContext::returnValue, because that
// window is not observable from the language — the handler entry writes the
// payload into a traced local slot before anything on the handler path
// allocates, and a freshly submitted young generation is not a candidate of the
// same collection cycle. Deleting the re-rooting line leaves this green; see
// docs/DECISIONS-LOG.md, E1, which says so rather than claiming otherwise.
def noise(n: Int): String = "x" + n
def deep(n: Int): Int =
  if n == 0 then throw new IllegalStateException("payload")
  else
    val s = noise(n)          // this frame now has a returnValue to anchor
    deep(n - 1) + s.length
@main def run(): Unit =
  var ok = 0
  var i = 0
  while i < 400 do
    try deep(60)
    catch case e: IllegalStateException => if e.getMessage == "payload" then ok += 1
    i += 1
  println("ok " + ok)
