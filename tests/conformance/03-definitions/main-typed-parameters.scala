// EXPECT-ERROR: typed @main parameters are not supported (D27)
@main def m(n: Int, s: String) = println(n + 1)
