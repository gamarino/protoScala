// EXPECT: found 3
def firstOver(limit: Int): Int =
  var i = 0
  while true do
    if i * i > limit then return i
    i += 1
  -1
@main def run(): Unit = println("found " + firstOver(5))
