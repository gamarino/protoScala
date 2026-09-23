// EXPECT: ()
// D45: when the handler gives no reply, the ask's future completes with `()`,
// so `?` is a `Future[Unit]` and `await` returns the unit value.
val sink = Actor.spawn(0) { (s, m) => s + m }
println((sink ? 7).await)
