// One parsed log line, and the parser that produces it.
//
// `Levels` is imported by its bare name: an import is resolved against the
// IMPORTING file's directory first, and this file sits next to Levels.scala.
// Writing `report.Levels` here would look for report/report/Levels.scala.
import Levels.{Level, fromLabel}

// A record, not a bag: naming the three fields is what lets the tally actor
// match on `Line(level, _, _)` instead of indexing into a list of strings.
case class Line(level: Level, source: String, message: String)

// The parser answers a `Try` rather than throwing, because a log file is
// expected to contain rubbish and the *caller* decides what a bad line costs.
// Everything inside the block may throw freely; `Try` turns the first throw
// into a `Failure` carrying the exception itself.
def parse(raw: String): Try[Line] = Try {
  val fields = raw.split(" ").toList.filter(f => f.nonEmpty)
  if fields.length < 3 then
    throw new IllegalArgumentException("expected LEVEL SOURCE MESSAGE, got '" + raw.trim + "'")
  Line(fromLabel(fields.head), fields(1), fields.drop(2).mkString(" "))
}
