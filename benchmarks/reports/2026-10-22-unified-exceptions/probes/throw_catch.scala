// EXPECT: CHECK 200000
// throw_catch: throw and catch in the same function, 200000 times.
def run(n: Int): Int = {
  var c = 0
  var i = 0
  while (i < n) {
    try { throw new RuntimeException("x") } catch { case e: RuntimeException => c = c + 1 }
    i = i + 1
  }
  c
}
@main def probeThrowCatch(): Unit = println("CHECK " + run(200000))
