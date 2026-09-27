// The module the foreign caller calls into (Phase 7 §D3, INTEROP §8).
//
// Nothing here is written for the demonstration: these are ordinary top-level defs,
// and protoscalac exports every one that captures nothing.

def add(a: Int, b: Int): Int = a + b

def sumTo(n: Int): Int = {
  var total = 0
  var i = 1
  while (i <= n) {
    total = total + i
    i = i + 1
  }
  total
}

def greet(who: String): String = "hello, " + who

def reciprocal(n: Int): Int =
  if (n == 0) throw new RuntimeException("division by zero") else 100 / n
