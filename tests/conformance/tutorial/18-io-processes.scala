// EXPECT: Nonzero exit value: 1
val today = Process(List("date", "+%Y")).!!.trim
val status = Process(List("sh", "-c", "exit 3")).!
val sorted = (Process(List("sort")) #< "pear\napple\nfig\n").!!
println(s"${today.length} $status ${sorted.split("\n").toList}")
val failed =
  try Process(List("false")).!!
  catch case e: RuntimeException => e.getMessage
println(failed)
