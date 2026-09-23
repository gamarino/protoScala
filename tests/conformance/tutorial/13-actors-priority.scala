// EXPECT: urgent
val inbox = Actor.spawn("") { (state, msg) => (msg.toString, msg.toString) }
inbox.send("routine", Priority.Low)
val answer = inbox.ask("urgent", Priority.High).await
println(answer)
