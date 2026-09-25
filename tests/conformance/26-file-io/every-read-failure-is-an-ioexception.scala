// EXPECT: missing=FileNotFoundException malformed=MalformedInputException dir=FileNotFoundException
// `catch case e: IOException` is what a Scala programmer writes, and it must see
// every one of these. That holds only because FileNotFoundException and
// MalformedInputException really are IOExceptions in the prelude's hierarchy
// (D97) AND because the runtime knows those class names -- a class the runtime
// cannot find is silently downgraded to RuntimeException, which this fixture
// would catch nothing of.
def classOfFailure(path: String): String =
  try
    Source.fromFile(path).mkString
    "no failure at all"
  catch case e: IOException => e.getClass
println("missing=" + classOfFailure("_data/nope.txt") +
        " malformed=" + classOfFailure("_data/malformed-utf8.bin") +
        " dir=" + classOfFailure("_data"))
