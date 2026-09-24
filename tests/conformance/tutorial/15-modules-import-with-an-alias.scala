// EXPECT: HELLO!
// `as` renames the module itself, which is Python's `import numpy as np`.
import util.Strings as S
@main def run(): Unit =
  println(S.shout("hello"))
