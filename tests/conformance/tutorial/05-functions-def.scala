// EXPECT: 7 hello, Ada
def add(a: Int, b: Int): Int = a + b
def greet(name: String): String = "hello, " + name
@main def run(): Unit =
  println(add(3, 4).toString + " " + greet("Ada"))
