#!/usr/bin/env protoscala
// EXPECT: 3
// Scala 3 skips an interpreter line when `#!` are the first two characters of
// the file, so a script can be run as a command. Indentation and line numbers
// after it are unaffected. scalac 3.9.0 compiles this file unchanged.
@main def run(): Unit =
  val x = 1 +
    2
  println(x)
