// EXPECT: utf-8/3 | pos=[] kw=[encodingOfTheOutput=utf-8,maximumRetryAttempts=3]
// The sharp edge of the interning convention: protoCore embeds a SHORT string in
// the pointer word itself, so a key built with a non-interning constructor works
// by accident for a short parameter name and silently drops the argument only for
// a name too long to embed. Both names below are long, on purpose, for a Scala
// callee and for the foreign stand-in alike. With a non-interned key the first
// value would read `none/0`.
def configure(encodingOfTheOutput: String = "none", maximumRetryAttempts: Int = 0): String =
  encodingOfTheOutput + "/" + maximumRetryAttempts
@main def run(): Unit =
  println(configure(maximumRetryAttempts = 3, encodingOfTheOutput = "utf-8") + " | " +
    __kwprobe.call(encodingOfTheOutput = "utf-8", maximumRetryAttempts = 3))
