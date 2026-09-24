// EXPECT: 20
// Re-running the same extension definition replaces it rather than colliding with
// itself: the installing native marks the member as an extension, so a genuine
// member of the type is still protected while a reloaded script or a repeated
// REPL line is not.
extension (n: Int) def scaled: Int = n * 3
extension (n: Int) def scaled: Int = n * 10
@main def run(): Unit = println(2.scaled)
