// EXPECT: caught at use
// A by-name argument's thunk runs where the name is READ, so the exception is
// raised inside `useIt`'s try, not at the call site. That is Scala's behaviour.
// `useIt` is a top-level def, which D53 lists as a call site the compiler
// resolves, so the argument really is lazy here.
def useIt(x: => Int): Int =
  try x catch case e: RuntimeException => { println("caught at use"); -1 }
def boom: Int = throw new RuntimeException("x")
@main def run(): Unit =
  useIt(boom)
