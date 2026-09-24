// EXPECT: Hello, World | she said "hi" | a<tab>b | 12
@main def run(): Unit =
  val plain = "Hello, World"
  val quoted = "she said \"hi\""
  val escaped = "a\tb"
  println(plain + " | " + quoted + " | " + escaped.split("\t").mkString("<tab>") + " | " +
    plain.length)
