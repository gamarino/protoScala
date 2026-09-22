// EXPECT: 3 visits, name=Ada
@main def run(): Unit =
  val name = "Ada"      // like a JS const, or a Python name you never rebind
  var visits = 0        // like a JS let
  visits += 1
  visits += 2
  println(visits.toString + " visits, name=" + name)
