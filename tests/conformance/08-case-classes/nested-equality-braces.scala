// EXPECT: true false true
case class Inner(v: Double)
case class Outer(i: Inner, tag: String)
@main def run(): Unit = {
  val a = Outer(Inner(1.0), "a")
  println((a == Outer(Inner(1.0), "a")).toString + " " + (a == Outer(Inner(2.0), "a")) + " " +
    (a.hashCode == Outer(Inner(1.0), "a").hashCode))
}
