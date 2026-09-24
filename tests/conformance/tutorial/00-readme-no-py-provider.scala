// EXPECT-ERROR: no provider registered for 'py'
// README, "What polyglot interop does and does not do today": the exact message
// the front page quotes, so the two cannot drift.
import py.numpy as np
@main def run(): Unit = println(np)
