// EXPECT: IllegalArgumentException: Bytes: 256 is not a byte (-128..255)
// A value outside a byte's range is refused rather than truncated: 256 would
// silently become 0.
try println(Bytes(1, 256))
catch case e: IllegalArgumentException => println(e.getClass + ": " + e.getMessage)
