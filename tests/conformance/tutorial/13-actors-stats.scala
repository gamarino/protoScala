// EXPECT: true
val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
val answer = (a ? 1).await
val stats = Actor.stats
println((answer == 1 && stats.workers >= 1 && stats.messagesProcessed >= 1).toString)
