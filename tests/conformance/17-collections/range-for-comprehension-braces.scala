// EXPECT: 285 List(0, 2, 4, 6, 8)
// The brace-syntax twin of range-for-comprehension.scala.
@main def run(): Unit = {
  var sum = 0
  for (i <- 0 until 10) {
    sum += i * i
  }
  val evens = for (i <- 0 until 10 if i % 2 == 0) yield i
  println(sum.toString + " " + evens)
}
