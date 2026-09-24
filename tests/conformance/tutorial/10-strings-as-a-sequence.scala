// EXPECT: 5 a s s a List(s, c, a, l, a) SCALA scl sca
@main def run(): Unit =
  val s = "scala"
  println(s.length.toString + " " + s(2) + " " + s.charAt(0) + " " + s.head + " " + s.last + " " +
    s.toList + " " + s.map(_.toUpper).mkString + " " + s.filter(_ != 'a') + " " +
    s.takeWhile(_ != 'l'))
