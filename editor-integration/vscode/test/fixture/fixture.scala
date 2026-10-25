// protoScala fixture for the VS Code extension tests: line numbers matter.
def twice(n: Int): Int =
  val doubled = n * 2
  doubled

def risky(n: Int): Int =
  throw new IllegalStateException("boom " + n)

println("e2e-before")
println(twice(21))
val r =
  try "caught " + risky(7)
  catch case e: IllegalStateException => "caught " + e.getMessage
println(r)
println("e2e-after")
