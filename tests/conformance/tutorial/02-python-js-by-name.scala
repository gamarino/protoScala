// EXPECT: cheap
@main def run(): Unit =
  def orElse(v: String, fallback: => String): String = if v != "" then v else fallback
  def expensive(): String =
    println("computing the fallback")
    "expensive"
  println(orElse("cheap", expensive()))
