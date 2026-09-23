// EXPECT: Success(7)
val echo = Actor.spawn(0) { (s, m) => (s, m) }
val done = Actor.spawn("") { (s, m) => (m.toString, m.toString) }
(echo ? 7).onComplete(t => done ! t.toString)
var seen = ""
while seen == "" do
  seen = done.value
println(seen)
