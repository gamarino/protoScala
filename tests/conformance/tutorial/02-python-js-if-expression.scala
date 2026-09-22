// EXPECT: minor adult
def category(age: Int): String = if age < 18 then "minor" else "adult"
@main def run(): Unit =
  println(category(12) + " " + category(40))
