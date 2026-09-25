// The input: the log file, read from disk.
//
// This module used to carry the text of `sample.log` a SECOND time, between two
// `"""` markers, because protoScala 0.6.0 had no file I/O and the program could
// not open its own input. A CLI check diffed the two copies so that a reader who
// edited the `.log` file and not the module would not silently see the old
// report. Track F removed the need for all of it: the program opens the file, so
// there is one copy and nothing to keep in step.
//
// The path arrives from `Main.scala`, which takes it from the command line. A
// relative path is resolved against the working directory, here exactly as on the
// JVM -- protoScala has no notion of "the directory the script is in", because
// Scala has none either.
def lines(path: String): List[String] =
  // A blank line is not a log entry. Nothing in `sample.log` is blank today; the
  // filter is here so that an edited log with a trailing newline or a stray blank
  // line still produces the same report rather than one extra malformed count.
  Source.fromFile(path).getLines().filter(l => l.trim.nonEmpty)
