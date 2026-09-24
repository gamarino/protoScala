// EXPECT: escaped
// A finally body that throws while the try is leaving by `return`: the cleanup
// is emitted inline before the RETURN, and the try's own Finally entry
// deliberately does NOT cover that inline copy — the try has already been left,
// so re-entering the same handler would re-raise a value it never saved.
def f(): Int =
  try
    return 7
  finally
    throw new IllegalStateException("from cleanup")
@main def run(): Unit =
  try println(f())
  catch case e: IllegalStateException => println("escaped")
