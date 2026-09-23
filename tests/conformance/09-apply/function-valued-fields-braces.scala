// EXPECT: 6 7 <function1>
class Ops(val inc: Int => Int) {
  val double = (x: Int) => x * 2
}

@main def run(): Unit = {
  val o = new Ops(x => x + 1)
  println(o.double(3).toString + " " + o.inc(6) + " " + o.double)
}
