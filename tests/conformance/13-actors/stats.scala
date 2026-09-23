// EXPECT: true
val a = Actor.spawn(0) { (s, m) => (s + 1, s + 1) }
var i = 0
while i < 100 do
  a ! 1
  i += 1
var seen = 0
while seen < 100 do
  seen = a.value
val st = Actor.stats
println((st.workers >= 1 && st.messagesProcessed >= 100).toString)
