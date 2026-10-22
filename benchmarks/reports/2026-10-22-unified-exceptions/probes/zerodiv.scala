// EXPECT: CHECK 200000
// zerodiv: native integer division by zero (1 / z), caught as ArithmeticException, 200000 times.
def run(n: Int, z: Int): Int = {
  var c = 0
  var i = 0
  while (i < n) {
    try { c = c + 1 / z } catch { case e: ArithmeticException => c = c + 1 }
    i = i + 1
  }
  c
}
@main def probeZeroDiv(): Unit = println("CHECK " + run(200000, 0))
