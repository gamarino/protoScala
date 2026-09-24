// EXPECT: Debug
object E {
  enum Level {
    case Debug, Info
  }
  def first(): Level = Level.values.head
}
@main def run(): Unit = println(E.first())
