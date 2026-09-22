// EXPECT: 1 2 fizz 4 buzz
def label(n: Int): String =
  if n % 3 == 0 then "fizz"
  else if n % 5 == 0 then "buzz"
  else n.toString

@main def run(): Unit =
  var out = ""
  var i = 1
  while i <= 5 do
    out = out + (if i == 1 then "" else " ") + label(i)
    i += 1
  println(out)
