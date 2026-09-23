// EXPECT: 49 49 1
var computed = 0
class Sq(n: Int) {
  lazy val value = {
    computed += 1
    n * n
  }
}

@main def run(): Unit = {
  val s = new Sq(7)
  println(s.value.toString + " " + s.value + " " + computed)
}
