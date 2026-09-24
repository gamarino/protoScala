// EXPECT: 1234.567800 1234.6 1.234568e+03 1.234568E+03 1.235e+03 1234.57
@main def run(): Unit =
  val x = 1234.5678
  println(f"$x%f $x%.1f $x%e $x%E $x%.3e $x%g")
