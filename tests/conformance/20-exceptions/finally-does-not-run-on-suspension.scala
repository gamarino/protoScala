// EXPECT: cleanup body=1
// The cleanup runs exactly ONCE, after the resume has finished the body — not
// when the frame suspends. A suspended frame is going to be resumed, not
// abandoned, so its finally has not been reached yet.
//
// The cleanup's evidence is a `print`, deliberately, and not a counter in a local
// variable: the frame snapshot is taken BEFORE the retry loop sees the
// suspension, so a counter incremented by a wrongly-run cleanup would be
// discarded by the resume and the fixture would pass while the bug was there.
// `cleanup cleanup body=1` is what a suspension that ran the finally prints.
@main def run(): Unit =
  val echo = Actor.spawn(0) { (s, m) => (s, m) }
  val counter = Actor.spawn(0) { (s, m) =>
    var body = 0
    try
      body = (echo ? 1).await
    finally
      print("cleanup ")
    (s, "body=" + body)
  }
  println((counter ? 1).await)
