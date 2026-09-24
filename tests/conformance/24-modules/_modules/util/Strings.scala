// A helper module for the 24-modules fixtures. Not a fixture itself: it lives
// under a `_`-prefixed directory, which the runner does not walk into.
def shout(s: String): String = s.toUpperCase + "!"
val greeting: String = "hello"
case class Tag(name: String, weight: Int)
def heavier(a: Tag, b: Tag): Tag = if a.weight >= b.weight then a else b
