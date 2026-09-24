// EXPECT: 9
// D82: an extension is session-wide, so importing the module that defines it
// makes it visible. Pinned so a later phase that scopes extensions flips a
// fixture rather than rediscovering the design.
import util.Extras
@main def run(): Unit = println(3.squared)
