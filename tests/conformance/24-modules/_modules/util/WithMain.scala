// A module may not carry an @main (D91): it is imported, not run.
@main def nope(): Unit = println("never")
