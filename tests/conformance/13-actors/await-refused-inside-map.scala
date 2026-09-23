// EXPECT: true
// D43: a call chain that crosses a native higher-order method cannot be
// snapshotted, so the await is refused; the ask fails and the actor survives.
val echo = Actor.spawn(0) { (s, m) => (s, m) }
val caller = Actor.spawn(0) { (s, m) =>
  val r = List(1, 2).map(x => (echo ? x).await)
  (r, r)
}
val f = caller ? 0
while !f.isCompleted do ()
val failed = f.value match
  case Some(Failure(e)) => e.className == "UnsupportedOperationException"
  case other            => false
println(failed.toString)
