// EXPECT: MILK | BREAD | APPLES
FileIO.write("shopping.txt", "milk\nbread\napples\n")

// `getLines()` answers an Iterator; `for`, `map` and `mkString` work on it.
val shouted = for line <- Source.fromFile("shopping.txt").getLines() yield line.toUpperCase
println(shouted.mkString(" | "))
