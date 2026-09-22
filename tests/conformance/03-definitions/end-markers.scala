// EXPECT: big 3
def classify(n: Int): String =
  if n > 10 then
    "big"
  else
    "small"
  end if
end classify

@main def run(): Unit =
  var i = 0
  while i < 3 do
    i += 1
  end while
  println(classify(42) + " " + i)
end run
