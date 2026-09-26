// EXPECT: Hello from a compiled module!
// Tutorial chapter 17.1. The smallest program there is, which is also the
// smallest thing protoscalac accepts: a top-level @main and a println.
//
// This fixture runs on the INTERPRETER like every other, and it is also in the
// differential harness, so the chapter's snippet is checked on both paths -- which
// is the only way a chapter about compiled modules can be honest about its own
// example.
@main def greet(): Unit =
  println("Hello from a compiled module!")
