// EXPECT: 100000 100010
// @tailrec on an extension method whose self-call is on its own receiver
// (`s.countChar(...)`), and on a final curried method called through the
// class's self alias; both loop (scalac 3.9 prints the same).
extension (s: String)
  @annotation.tailrec
  def countChar(c: Char, i: Int, acc: Int): Int =
    if i == s.length then acc else s.countChar(c, i + 1, if s(i) == c then acc + 1 else acc)

class Stepper(val k: Int):
  self =>
  @annotation.tailrec
  final def walk(n: Int)(acc: Int): Int = if n == 0 then acc + k else self.walk(n - 1)(acc + 1)

@main def run(): Unit =
  val text = "ab" * 100000
  println(s"${text.countChar('a', 0, 0)} ${new Stepper(10).walk(100000)(0)}")
