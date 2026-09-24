// EXPECT: pos=[] kw=[a=1,b=2,c=3]
// If a key were built with a non-interning constructor it would match nothing and
// the arguments would be silently dropped, so this prints every value. The report
// is sorted by name, so the order the keys happen to sit in cannot make it flaky.
@main def run(): Unit =
  println(__kwprobe.call(c = 3, a = 1, b = 2))
