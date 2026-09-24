// EXPECT: true
// PROTO_NONE is both Scala `null` and "attribute missing". A member that
// legitimately holds null must import as null, not be reported absent -- which
// is what probing with hasAttribute buys over comparing the value.
import js.probe.{nothing}
@main def run(): Unit = println(nothing == null)
