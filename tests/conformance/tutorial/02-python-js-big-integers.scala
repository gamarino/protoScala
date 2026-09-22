// EXPECT: 1267650600228229401496703205376
// Like Python, integers never overflow (D1). This is 2 to the power 100.
@main def run(): Unit =
  var result: BigInt = 1
  var i = 0
  while i < 100 do
    result = result * 2
    i += 1
  println(result)
