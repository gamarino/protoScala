// EXPECT: 7 12
extension (n: Int) {
  def plus(m: Int): Int = n + m
  def times(m: Int): Int = n * m
}
@main def run(): Unit = println(3.plus(4).toString + " " + 3.times(4))
