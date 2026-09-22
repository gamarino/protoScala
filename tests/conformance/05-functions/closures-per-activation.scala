// EXPECT: 0 1
// Each loop iteration's `val j` is a fresh binding (DESIGN §3.5).
@main def run(): Unit =
  var f0: () => Int = null
  var f1: () => Int = null
  var i = 0
  while i < 2 do
    val j = i
    val f = () => j
    if i == 0 then f0 = f else f1 = f
    i += 1
  println(f0().toString + " " + f1())
