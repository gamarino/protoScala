// EXPECT: true
import util.Strings.{Tag}
@main def run(): Unit =
  val v: Any = Tag("a", 1)
  println(v.isInstanceOf[Tag])
