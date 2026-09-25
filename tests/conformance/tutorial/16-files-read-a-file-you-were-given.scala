// EXPECT: MILK | BREAD | APPLES
FileIO.write("shopping.txt", "milk\nbread\napples\n")

// `getLines()` answers a List, so everything you know about List applies.
val shouted = for line <- Source.fromFile("shopping.txt").getLines() yield line.toUpperCase
println(shouted.mkString(" | "))
