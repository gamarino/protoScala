// EXPECT-ERROR: a wildcard import of a foreign module is not supported
// D92: a foreign object's attribute names cannot be enumerated, and guessing a
// name set would fail silently later. The message names the working spelling.
import js.probe.*
@main def run(): Unit = println(1)
