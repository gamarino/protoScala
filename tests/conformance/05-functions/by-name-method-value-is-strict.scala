// EXPECT: 2
// D53: a `def` used as a function value loses its by-name signature, so the
// argument is evaluated once at the call and read twice from the value
// (1 + 1 = 2). Scala 3 eta-expands to a by-name function type and prints 3.
def twiceOf(body: => Int): Int = body + body
val f = twiceOf
var n = 0
println(f({ n = n + 1; n }))
