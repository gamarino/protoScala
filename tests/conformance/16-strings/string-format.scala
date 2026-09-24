// EXPECT: n=007
// `format` goes through the same formatter as the f-interpolator, so the two
// cannot disagree. Verified against tools/scala3-3.9.0.
@main def run(): Unit =
  println("n=%03d".format(7))
