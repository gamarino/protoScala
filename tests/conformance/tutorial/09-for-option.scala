// EXPECT: Some(3) None
def parse(s: String): Option[Int] = if s == "1" then Some(1) else if s == "2" then Some(2) else None

@main def run(): Unit =
  val ok = for (a <- parse("1"); b <- parse("2")) yield a + b
  val bad = for (a <- parse("1"); b <- parse("x")) yield a + b
  println(ok.toString + " " + bad)
