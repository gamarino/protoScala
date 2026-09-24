// EXPECT-ERROR: no provider registered for 'py'
// The py. prefix routes. What is missing is the provider, not the routing: no
// runtime in the family registers the UMD alias `py` (INTEROP §3, Track Y).
import py.numpy as np
@main def run(): Unit = println(np)
