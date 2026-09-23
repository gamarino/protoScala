// EXPECT: 158
// The README's "A flavour of the language" snippet, verbatim: the claim that
// every line runs today is a test, not a promise.
case class Increment(by: Int)
case object GetValue

val counter = Actor.spawn(0) { (state, msg) =>
  msg match
    case Increment(by) => (state + by, state + by)
    case GetValue      => (state, state)
}

counter ! Increment(10)
println((counter ? GetValue).await)          // 10
println(Future(6 * 7).await)                 // 42 — Future takes its body by name

val squares = for x <- List(1, 2, 3) yield x * x
println(squares)                              // List(1, 4, 9)

def factorial(n: Int): Int = if n == 0 then 1 else n * factorial(n - 1)
println(factorial(100).toString.length)       // 158 — integers never overflow
