// EXPECT: Dear Ada, /   two spaces of body indent survive / Yours, Alan
@main def run(): Unit =
  val letter = """|Dear Ada,
                  |  two spaces of body indent survive
                  |Yours, Alan""".stripMargin
  println(letter.split("\n").mkString(" / "))
