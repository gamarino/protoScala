// EXPECT: Debug
// An `enum` nested in an `object` is expanded, lifted and usable. It was not:
// expandEnums walked only the unit's top-level statements, so a nested `enum`
// reached liftNestedTemplates unexpanded and every reference to it failed with
// "Not found". A `case class` in the same position always worked, which is what
// made the gap invisible.
object E:
  enum Level:
    case Debug, Info
  def first(): Level = Level.values.head
@main def run(): Unit = println(E.first())
