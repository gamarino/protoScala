// EXPECT-ERROR: would call itself
// super[C].m inside C would find C's own m and recurse for ever, so it is a
// compile error rather than a stack overflow.
trait A:
  def m: Int = 1
class C extends A:
  override def m: Int = super[C].m
@main def run(): Unit = println(new C().m)
