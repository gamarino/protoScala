// EXPECT: CHECK 100000
// throw_across5: throw 5 frames below the handler, 100000 times.
def deep(d: Int): Int =
  if (d == 0) throw new RuntimeException("deep") else 1 + deep(d - 1)
def run(n: Int): Int = {
  var c = 0
  var i = 0
  while (i < n) {
    try { c = c + deep(5) } catch { case e: RuntimeException => c = c + 1 }
    i = i + 1
  }
  c
}
@main def probeThrowAcross5(): Unit = println("CHECK " + run(100000))
