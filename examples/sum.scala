#!/usr/bin/env protoscala
// A Unix filter run as a command: it adds the integers read from standard
// input, one per line, and prints the total. Blank lines are skipped. A line
// that is not an integer is reported on standard error with its line number
// and the program exits with status 1, so `&&` and `set -e` stop the
// pipeline. With empty input the total is 0.
//
//   seq 1 100 | ./sum.scala                                          # 5050
//   printf 'pump-1 12\npump-2 30\n' | ./column.scala 2 | ./sum.scala   # 42
@main def sum(): Unit =
  var total = 0L
  var lineNumber = 0
  var line = StdIn.readLine()
  while line != null do
    lineNumber += 1
    val text = line.trim
    if text.nonEmpty then
      Try(text.toLong) match
        case Success(value) => total += value
        case Failure(_) =>
          Console.err.println(s"sum.scala: line $lineNumber is not a number: $line")
          sys.exit(1)
    line = StdIn.readLine()
  println(total)
