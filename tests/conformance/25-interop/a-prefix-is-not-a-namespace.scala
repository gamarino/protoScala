// EXPECT: HELLO!
// A directory called `py` beside the module tree is NOT reached by `import
// py.Helper`: segment 0 is a family prefix and goes to the provider. Reached
// under a different first segment, the same file imports normally.
import local.py.Helper
@main def run(): Unit = println(Helper.shout("hello"))
