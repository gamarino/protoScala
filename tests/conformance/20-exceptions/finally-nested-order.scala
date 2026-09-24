// EXPECT: inner outer done
// Innermost first: this is the case where an implementation drifts silently.
def f(): Int =
  try
    try
      return 1
    finally
      print("inner ")
  finally
    print("outer ")
@main def run(): Unit =
  f()
  println("done")
