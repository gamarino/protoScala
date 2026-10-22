// EXPECT: CHECK 2000000
// try_no_throw: enter a try whose body does not throw, 2000000 times.
def run(n: Int): Int = {
  var c = 0
  var i = 0
  while (i < n) {
    try { c = c + 1 } catch { case e: RuntimeException => c = c - 1 }
    i = i + 1
  }
  c
}
@main def probeTryNoThrow(): Unit = println("CHECK " + run(2000000))
