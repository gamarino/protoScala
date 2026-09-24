// EXPECT: Tag(b,3)
// The fixture that proves an imported TYPE is usable: `heavier` takes Tags and
// `Tag(...)` is the companion the import brought with it.
import util.Strings.{Tag, heavier}
@main def run(): Unit = println(heavier(Tag("a", 1), Tag("b", 3)))
