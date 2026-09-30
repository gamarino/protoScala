// EXPECT: 0 lines, the longest is ''
var count = 0
var longest = ""
var line = StdIn.readLine()
while line != null do
  count += 1
  if line.length > longest.length then longest = line
  line = StdIn.readLine()
println(s"$count lines, the longest is '$longest'")
