// EXPECT: (1,a) 1 a true (1,(2,3)) 3 (1,a,true) 3
@main def run(): Unit = {
  val t = (1, "a")
  val nested = (1, (2, 3))
  val t3 = (1, "a", true)
  println(t.toString + " " + t._1 + " " + t._2 + " " + (t == (1, "a")) + " " + nested + " " +
    nested._2._2 + " " + t3 + " " + t3.productArity)
}
