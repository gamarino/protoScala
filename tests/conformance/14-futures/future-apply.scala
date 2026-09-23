// EXPECT: 9
val f = Future(() => 3 * 3)
println(f.await)
