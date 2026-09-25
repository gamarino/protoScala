// EXPECT-ERROR: FileNotFoundException: . (Is a directory)
// "." is the working directory. Asking to read it fails, and says why -- it does
// not hand you an empty string.
println(Source.fromFile(".").mkString)
