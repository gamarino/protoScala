// EXPECT: (Ada,36) Ada 36 true
@main def run(): Unit =
  val person = ("Ada", 36)
  val (name, age) = person
  println(person.toString + " " + name + " " + age + " " + (person == ("Ada", 36)))
