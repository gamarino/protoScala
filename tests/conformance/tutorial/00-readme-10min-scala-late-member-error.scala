// EXPECT-ERROR: value lenght is not a member of String
// The other half of point 1: late detection, never silent.
@main def run(): Unit = println("abc".lenght)
