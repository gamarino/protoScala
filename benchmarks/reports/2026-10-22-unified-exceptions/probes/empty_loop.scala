// EXPECT: CHECK 2000000
// empty_loop: the loop alone, 2000000 iterations.
def run(n: Int): Int = {
  var c = 0
  var i = 0
  while (i < n) { c = c + 1; i = i + 1 }
  c
}
@main def probeEmptyLoop(): Unit = println("CHECK " + run(2000000))
