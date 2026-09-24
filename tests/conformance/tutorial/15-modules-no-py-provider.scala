// EXPECT-ERROR: no provider registered for 'py'
// The prefix routes; what is missing is the runtime that answers to it.
import py.numpy as np
@main def run(): Unit = println(np)
