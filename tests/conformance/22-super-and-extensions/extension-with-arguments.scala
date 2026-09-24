// EXPECT: 7 12
// A collective extension, in the only indented spelling scalac accepts: no `:`
// before the members.
extension (n: Int)
  def plus(m: Int): Int = n + m
  def times(m: Int): Int = n * m
@main def run(): Unit = println(3.plus(4).toString + " " + 3.times(4))
