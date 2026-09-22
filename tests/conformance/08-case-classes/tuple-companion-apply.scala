// EXPECT: (1,2) true (a,2,true) 22 22
@main def run(): Unit =
  val t22 = Tuple22(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22)
  println(Tuple2(1, 2).toString + " " + (Tuple2(1, 2) == (1, 2)) + " " + Tuple3("a", 2, true) +
    " " + t22._22 + " " + t22.productArity)
