// EXPECT-ERROR: already has a member named 'length'
// Silently shadowing a builtin method would be unrecoverable within a session
// (D83), so a collision is refused. scalac allows the shadowing — the member wins
// and the extension is simply never reached — so this diagnostic is protoScala's
// own. The check runs at compile time where the compiler describes the type's
// members, and in the installing native for a primitive prototype, whose members
// it does not.
extension (s: String) def length: Int = 0
@main def run(): Unit = println("x".length)
