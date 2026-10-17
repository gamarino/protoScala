#!/usr/bin/env protoscala
// EXPECT: 0
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
