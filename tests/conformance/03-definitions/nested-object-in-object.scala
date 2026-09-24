// EXPECT: 5
object Outer:
  object Inner:
    val v: Int = 5
@main def run(): Unit = println(Outer.Inner.v)
