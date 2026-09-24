// EXPECT: 5 5
// The retry loop must not grow the native stack per caught exception: entering a
// handler is a `continue` in the SAME frame, not a recursive call.
@main def run(): Unit =
  var caught = 0
  var ok = 0
  var i = 0
  while i < 10 do
    try
      if i % 2 == 0 then throw new RuntimeException("x") else ok += 1
    catch
      case e: RuntimeException => caught += 1
    i += 1
  println(caught.toString + " " + ok)
