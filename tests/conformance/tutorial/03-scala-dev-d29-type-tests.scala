// EXPECT: true true
@main def run(): Unit =
  println(3L.isInstanceOf[Int].toString + " " + (1L << 40).isInstanceOf[Int])
