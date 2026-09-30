// EXPECT: A,B|2|List((a,0), (b,1), (c,2))|abc|true|Some(b)|a-b-c|<a;b;c>|6|a,b,false
// The Iterator a source answers carries the usual operations; each call below
// uses a fresh iterator, because an Iterator is consumed as it is read.
def it() = Source.fromString("a\nb\nc").getLines()
val upper = it().map(_.toUpperCase).take(2).mkString(",")
val count = it().count(_ != "a")
val zipped = it().zipWithIndex.toList
val folded = it().foldLeft("")(_ + _)
val exists = it().exists(_ == "c")
val found = it().find(_ > "a")
val dashed = it().mkString("-")
val framed = it().mkString("<", ";", ">")
var total = 0
for s <- it() do total += s.length * 2
// The protocol itself: hasNext and next().
val manual = it()
val first = manual.next()
val second = manual.next()
manual.next()
println(upper + "|" + count + "|" + zipped + "|" + folded + "|" + exists + "|" +
        found + "|" + dashed + "|" + framed + "|" + total + "|" + first + "," + second + "," +
        manual.hasNext)
