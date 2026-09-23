// EXPECT: 8
val f = Future.successful(4)
println(f.map(x => x * 2).await)
