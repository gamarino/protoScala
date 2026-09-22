// EXPECT: 3
@main def run(): Unit =
  var calls = 0
  def tick = { calls += 1; calls }
  tick
  tick
  println(tick)
