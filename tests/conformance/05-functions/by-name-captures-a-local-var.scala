// EXPECT: 3
// The thunk the call site builds captures the enclosing `var` by reference, so
// the three evaluations accumulate (D47).
def count(): Int =
  var c = 0
  def thrice(body: => Unit): Unit =
    body
    body
    body
  thrice({ c = c + 1 })
  c
println(count())
