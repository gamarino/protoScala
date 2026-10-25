class Greeter:
  def greet(name: String, punct: String = "!"): String = "Hi " + name + punct

def check(x: Int, s: String): Boolean = x >= 0 && s != null || x == -1

val g = new Greeter()
println(g.greet("Bo", punct = "?"))
println(check(1, null))
try throw new RuntimeException("boom")
catch case e: Exception => println(e.getMessage)
