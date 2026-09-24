// EXPECT: ready: true
// util/effects/Setup.scala prints `setup ran` from its top level, and it does so
// while this file is being compiled: an import is resolved by loading (D90).
import effects.Setup
@main def run(): Unit =
  println("ready: " + Setup.ready)
