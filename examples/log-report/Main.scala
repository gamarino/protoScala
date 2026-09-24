// A log report in one program: modules, an enum, case classes and patterns,
// Try and catch, Map aggregation, for-yield, interpolation, and actors.
//
// Run from anywhere: protoscala examples/log-report/Main.scala
//
// The three imports below are resolved against THIS file's directory first, so
// the example is self-contained and needs no PROTOSCALA_PATH and no build tool.
import report.Levels.Level
import report.Lines.{Line, parse}
import report.Sample

// The one message the tally actor answers rather than absorbs. A case object is
// a value with a name, which is all a protocol marker needs to be.
case object Report

@main def run(args: String*): Unit =
  // How many parsers to fan out over. An argument makes the fan width easy to
  // play with; the result must not depend on it, and that is the point.
  val width = if args.isEmpty then 3 else args(0).toInt

  // Each parser is an actor whose state is its own index -- it needs no state
  // at all, but an actor must have one, and the index makes it identifiable.
  val parsers = for i <- (0 until width).toList yield
    Actor.spawn(i) { (id, line) => (id, parse(line.toString)) }

  // The ask is what fans the work out: `?` queues the line and hands back a
  // Future immediately, so every parser is busy before the first answer is
  // read. Round-robin keeps the distribution independent of line length.
  val answers = for pair <- Sample.lines.zipWithIndex yield
    parsers(pair._2 % width) ? pair._1

  // The fold-back point. One actor owns the counts, so no lock is needed and
  // no two updates can interleave: the single-method invariant does that work.
  val tally = Actor.spawn(Map[String, Int]()) { (counts, msg) =>
    msg match
      case Line(level, _, _) =>
        val key = level.label
        (counts.updated(key, counts.getOrElse(key, 0) + 1), ())
      case Report => (counts, counts)
  }

  // Awaiting in source order is what makes the output deterministic even though
  // the parsing was not. `.get` on a Failure re-raises the parser's own
  // exception, so a malformed line is handled here, once, where the policy is.
  var malformed = 0
  var i = 0
  while i < answers.length do
    try tally ! answers(i).await.get
    catch case e: IllegalArgumentException => malformed = malformed + 1
    i = i + 1

  // Sent last on the same priority band, so it is handled last: the counts this
  // returns have seen every line above.
  val counts = (tally ? Report).await

  // The enum's declaration order is the report's order. Nothing here names a
  // level, so adding a case to Level adds a column and changes nothing else.
  val columns = for l <- Level.values yield s"$l ${counts.getOrElse(l.label, 0)}"
  println(s"${columns.mkString(" | ")} | malformed $malformed")
