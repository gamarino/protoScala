// EXPECT: 285 List(0, 2, 4, 6, 8)
// D68: a `for ... yield` over a Range gives a List here and a Vector in Scala.
// The elements and their order are Scala's; verified against tools/scala3-3.9.0.
@main def run(): Unit =
  var sum = 0
  for i <- 0 until 10 do
    sum += i * i
  val evens = for i <- 0 until 10 if i % 2 == 0 yield i
  println(sum.toString + " " + evens)
