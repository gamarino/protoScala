// EXPECT: 1 1
// A0-5 ruling C1 (2026-09-23): a Char follows Scala. Its `==` is not `eq` --
// 'a' == 97 is true and 'a'.## is 97 -- so it is a value key and 'a' and 97 are
// ONE key. Under the old reading ('a' an identity key, 97 a value key) this
// would print `2 1`. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  val m = Map[Any, Int]('a' -> 1, 97 -> 1)
  println(m.size.toString + " " + m.getOrElse(97, -1))
