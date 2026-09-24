// EXPECT: 9
// D82: an extension is session-wide. Importing the module that defines one makes
// it visible to the importing file. Pinned so a later phase that scopes
// extensions flips a fixture rather than rediscovering the design.
import util.Extras
@main def run(): Unit = println(3.squared)
