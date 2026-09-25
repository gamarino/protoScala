// A module the 28-member-imports fixtures import by path, to check that adding
// plain Scala's member import did not shadow Phase 6's module loading.
def shout(s: String): String = s.toUpperCase + "!"
