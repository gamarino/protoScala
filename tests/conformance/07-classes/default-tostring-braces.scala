// EXPECT: true false
class Plain(val a: Int)
@main def run(): Unit = {
  val p = new Plain(1)
  println(p.toString.startsWith("Plain@").toString + " " + (p == new Plain(1)))
}
