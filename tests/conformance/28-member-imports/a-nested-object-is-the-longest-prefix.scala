// EXPECT: 7
// The prefix is the LONGEST dotted path that names something in scope, which is
// the same rule the module loader uses. Here `B1` is in scope and so is `B1.B2`,
// and it is `B1.B2` that must win, or `q` would be looked for on `B1`.
object B1:
  object B2:
    val q = 7
import B1.B2.q
@main def run(): Unit = println(q)
