// EXPECT: HELLO!
// The regression guard in the other direction: adding the member-import form must
// not shadow Phase 6's module loading. Nothing named `util` is in scope here, so
// `util.Strings` is still resolved by loading the file.
import util.Strings.shout
@main def run(): Unit = println(shout("hello"))
