// EXPECT: warm 2
// A singleton enum case is a case OBJECT, so it carries the synthesised
// product_equals and DESIGN §6.1 bullet 2 makes it a VALUE key — which is
// indistinguishable from an identity key here, because a case object has exactly
// one instance. Phase 3's XFAIL comment claimed bullet 1 and an identity key;
// Phase 4 measured it (tests/unit/test_collections.cpp,
// MapSurface.EnumCaseSlotKindIsTheCaseClassOneForBothKindsOfCase) and corrected
// the claim. The answer this fixture pins was right either way, which is exactly
// why the white-box test is the one that can tell.
enum Color:
  case Red, Blue
@main def run(): Unit =
  val m = Map(Color.Red -> "warm", Color.Blue -> "cool")
  println(m(Color.Red) + " " + m.size)
