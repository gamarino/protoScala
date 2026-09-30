// EXPECT: 2 3 0 2 1
// String.split follows Java's rule: when the separator occurs, trailing empty
// strings are removed, so a line-terminated text splits into its lines with no
// empty last element; leading and inner empty strings stay; a string without the
// separator is answered whole. Verified against scalac 3.9.0, which prints
// List(a, b), List(a, , b), List(), List(, a) and a one-element list for "".
println("a\nb\n".split("\n").length.toString + " " + "a,,b,,".split(",").length + " " +
        ",,".split(",").length + " " + ",a".split(",").length + " " + "".split(",").length)
