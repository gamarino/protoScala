#!/usr/bin/env protoscala
// A Unix filter run as a command: it prints the N-th space-separated field
// of every line of standard input, like `awk '{print $N}'`. A line with
// fewer fields prints nothing. The first line is an interpreter line, which
// Scala 3 skips, so once the file is executable (chmod +x column.scala) it
// chains with other commands:
//
//   printf 'pump-1 12\npump-2 30\n' | ./column.scala 2 | ./sum.scala    # 42
//
// Without N it prints a usage message on standard error and exits with
// status 2, the convention of the Unix tools.
@main def column(args: String*): Unit =
  val n = if args.isEmpty then 0 else Try(args(0).toInt).getOrElse(0)
  if n < 1 then
    Console.err.println("usage: column.scala N   (N >= 1)")
    sys.exit(2)
  var line = StdIn.readLine()
  while line != null do
    val fields = line.split(" ").filter(f => f.nonEmpty)
    if fields.length >= n then println(fields(n - 1))
    line = StdIn.readLine()
