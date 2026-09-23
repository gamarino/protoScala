// EXPECT: taken
// The unused by-name argument never runs, so its `println` produces no output
// and the last line is the one the body prints (D47).
def firstOf(a: => String, b: => String): String = a
println(firstOf("taken", { println("evaluated"); "skipped" }))
