// EXPECT: true
// Two imports of the same module must reach the SAME object. This is what
// detects a broken exactly-once load: the earlier version of this fixture
// printed `Setup.ready`, and a module loaded twice still answers `true`, so it
// could not tell one load from two. `eq` can -- a second load compiles the
// module again and produces a different singleton.
import effects.Setup
import effects.Setup as Again
@main def run(): Unit = println(Setup eq Again)
