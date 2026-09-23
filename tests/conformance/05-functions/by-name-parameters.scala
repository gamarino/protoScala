// EXPECT: 3
// A by-name parameter is not evaluated at the call site and is re-evaluated at
// every use in the body, as in Scala (D47).
var n = 0
def twice(body: => Int): Int = body + body
val sum = twice({ n = n + 1; n })
println(sum)
