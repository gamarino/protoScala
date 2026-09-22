// EXPECT: 6 6
def sumIndented(n: Int): Int =
  var total = 0
  var i = 1
  while i <= n do
    total += i
    i += 1
  total

def sumBraces(n: Int): Int = {
  var total = 0
  var i = 1
  while (i <= n) {
    total += i
    i += 1
  }
  total
}

@main def run(): Unit =
  println(sumIndented(3).toString + " " + sumBraces(3))
