// EXPECT: ALPHA BETA GAMMA
// `getLines()` answers a List, so every List operation works on it directly --
// including a `for` over it, which is how most programs read a file.
var out = ""
for line <- Source.fromFile("_data/three.txt").getLines() do
  out = out + line.toUpperCase + " "
println(out.trim)
