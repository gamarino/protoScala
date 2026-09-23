// EXPECT: true
val a = System.nanoTime()
var i = 0
while i < 100000 do
  i += 1
val b = System.nanoTime()
println((b >= a && System.currentTimeMillis() > 0 && System.getenv("PROTOSCALA_NO_SUCH_VAR") == "").toString)
