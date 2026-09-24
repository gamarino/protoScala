// EXPECT: w=2 h=3
// The keyword key is the ADDRESS of the interned parameter-name symbol, obtained
// with ProtoString::createSymbol. A key built from a non-interning constructor
// (fromUTF8String, fromUTF8, fromStdString) returns a different pointer for the
// same text, matches nothing, and the argument is SILENTLY dropped — so this
// fixture prints the BOUND VALUES rather than merely calling without crashing.
// With a non-interned key both parameters would take their defaults and the
// output would be `w=0 h=0`, a visible and unambiguous failure.
def describe(w: Int = 0, h: Int = 0): String = "w=" + w + " h=" + h
@main def run(): Unit = println(describe(h = 3, w = 2))
