// A helper module for the chapter 15 fixtures. It lives under a `_`-prefixed
// directory, which the runner does not walk into, so it is never run on its own.
def shout(s: String): String = s.toUpperCase + "!"
def initials(s: String): String = s.split(" ").map(w => w.substring(0, 1)).mkString(".")
val greeting: String = "hello"
