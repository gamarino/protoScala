// EXPECT: 6 empty one:9 first=1,second=2,rest=List(3)
def sum(xs: List[Int]): Int = xs match {
  case Nil => 0
  case h :: t => h + sum(t)
}
def shape(xs: List[Int]): String = xs match {
  case Nil => "empty"
  case x :: Nil => "one:" + x
  case a :: b :: rest => "first=" + a + ",second=" + b + ",rest=" + rest
}

@main def run(): Unit = {
  println(sum(List(1, 2, 3)).toString + " " + shape(Nil) + " " + shape(List(9)) + " " + shape(List(1, 2, 3)))
}
