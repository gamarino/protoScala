// EXPECT: true
// The module prints `setup ran` before the program prints anything (D90).
import effects.Setup
@main def run(): Unit = println(Setup.ready)
