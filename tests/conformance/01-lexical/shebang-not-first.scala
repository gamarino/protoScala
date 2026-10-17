// EXPECT-ERROR: expression expected
// `#!` is an interpreter line only at the very start of a file. Anywhere else
// it is an operator, so this file does not parse; scalac 3.9.0 rejects it
// with the same message.
val x = 1
#! not first
