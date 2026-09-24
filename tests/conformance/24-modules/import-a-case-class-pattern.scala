// EXPECT: b
// An imported case class in an extractor pattern: a name bound at RUN time
// would carry no ClassInfo and this would not compile (plan A0-1).
import util.Strings.{Tag}
@main def run(): Unit =
  Tag("b", 3) match
    case Tag(n, _) => println(n)
