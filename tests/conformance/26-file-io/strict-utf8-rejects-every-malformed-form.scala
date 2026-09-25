// EXPECT: overlong=0 surrogate=0 out-of-range=0
// The three malformed forms a lenient decoder lets through, and that a file
// arriving from another system really does contain: an OVERLONG encoding
// (C0 AF, "/" written in two bytes), a SURROGATE code point (ED A0 80, U+D800 --
// legal in UTF-16, never in UTF-8) and a code point ABOVE U+10FFFF (F5 80 80 80).
//
// scalac 3.9.0 raises java.nio.charset.MalformedInputException for all three,
// because the JVM's UTF-8 decoder is strict. So is this one, and the offset it
// reports is the start of the offending sequence.
//
// A decoder that accepted any of them would hand the program a String that does
// not round-trip and, for the overlong form, one that can smuggle a "/" past a
// path check.
def offsetOf(name: String): String =
  try
    Source.fromFile("_data/" + name).mkString
    "ACCEPTED"
  catch
    case e: MalformedInputException =>
      e.getMessage.substring(e.getMessage.lastIndexOf(" ") + 1)
println("overlong=" + offsetOf("overlong-utf8.bin") +
        " surrogate=" + offsetOf("surrogate-utf8.bin") +
        " out-of-range=" + offsetOf("out-of-range-utf8.bin"))
