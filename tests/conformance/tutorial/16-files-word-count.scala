// EXPECT: errors=2 words=14
FileIO.write("app.log",
  "INFO started\nERROR disk is full\nINFO retrying in 5 seconds\nERROR giving up\n")

val lines = Source.fromFile("app.log").getLines()
val errors = lines.count(l => l.startsWith("ERROR"))
val words = lines.map(l => l.split(" ").length).sum
println(s"errors=$errors words=$words")
