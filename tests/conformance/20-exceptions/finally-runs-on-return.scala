// EXPECT: finally 7
// Scala evaluates the returned expression first, then runs the enclosing
// finally bodies, then leaves the method.
def f(): Int =
  try
    return 7
  finally
    print("finally ")
@main def run(): Unit = println(f())
