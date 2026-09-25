// EXPECT: rg
// An enum's cases are lifted to top-level definitions with dotted names
// (`Color.Red`), so they are not members of the companion and a wildcard that
// only walked the companion's members would bind nothing. This fixture is what
// pins that they are picked up: the cases have to work as *patterns* under their
// simple names, not merely as expressions.
enum Color:
  case Red, Green
import Color.*
def name(c: Color): String = c match
  case Red   => "r"
  case Green => "g"
@main def run(): Unit = println(name(Red) + name(Green))
