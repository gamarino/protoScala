// Uses an extension it does NOT import. Under D82 an extension is session-wide,
// so this compiles and runs when something else in the session has loaded
// util.Extras, and fails at run time when nothing has.
def viaExtension(n: Int): Int = n.squared
