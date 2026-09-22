#include "EvalHarness.h"

#include <gtest/gtest.h>

using protoScala::test::EvalHarness;

namespace {
bool isError(const std::string& r, const std::string& cls) {
    return r.rfind("error: " + cls, 0) == 0;
}
}

TEST(Primitives, IntMethods) {
    EvalHarness h;
    EXPECT_EQ(h.eval("1.toString + 2"), "12");
    EXPECT_EQ(h.eval("7 / 2"), "3");
    EXPECT_EQ(h.eval("-7 / 2"), "-3");
    EXPECT_EQ(h.eval("-7 % 2"), "-1");
    EXPECT_EQ(h.eval("7 % -2"), "1");
    EXPECT_EQ(h.eval("3.max(7)"), "7");
    EXPECT_EQ(h.eval("-3.abs"), "3");
    EXPECT_EQ(h.eval("10.toDouble"), "10.0");
    EXPECT_EQ(h.eval("98.toChar"), "b");
    EXPECT_EQ(h.eval("6 & 3"), "2");
    EXPECT_EQ(h.eval("1 << 60"), "1152921504606846976");
    EXPECT_EQ(h.eval("1 / 0"), "error: ArithmeticException: / by zero");
    EXPECT_TRUE(isError(h.eval("8 >>> 1"), "UnsupportedOperationException"));
}

TEST(Primitives, DoubleMethods) {
    EvalHarness h;
    EXPECT_EQ(h.eval("7.0 / 2"), "3.5");
    EXPECT_EQ(h.eval("5.5 % 2"), "1.5");
    EXPECT_EQ(h.eval("1.0 / 0"), "Infinity");
    EXPECT_EQ(h.eval("(0.0 / 0.0).isNaN"), "true");
    EXPECT_EQ(h.eval("3.7.toInt"), "3");
    EXPECT_EQ(h.eval("-3.7.toInt"), "-3");
    EXPECT_EQ(h.eval("2.5.round"), "3");
    EXPECT_EQ(h.eval("2.5.floor"), "2.0");
}

TEST(Primitives, CharAndBoolean) {
    EvalHarness h;
    EXPECT_EQ(h.eval("'a'.toInt"), "97");
    EXPECT_EQ(h.eval("'7'.isDigit"), "true");
    EXPECT_EQ(h.eval("'q'.toUpper"), "Q");
    EXPECT_EQ(h.eval("true & false"), "false");
    EXPECT_EQ(h.eval("true ^ true"), "false");
}

TEST(Primitives, StringMethods) {
    EvalHarness h;
    EXPECT_EQ(h.eval("\"hello\".length"), "5");
    EXPECT_EQ(h.eval("\"hello\".toUpperCase"), "HELLO");
    EXPECT_EQ(h.eval("\"hello\".substring(1, 3)"), "el");
    EXPECT_EQ(h.eval("\"hello\".substring(3)"), "lo");
    EXPECT_EQ(h.eval("\"hello\"(1)"), "e");
    EXPECT_EQ(h.eval("\"hello\".charAt(0)"), "h");
    EXPECT_EQ(h.eval("\"ab\" * 3"), "ababab");
    EXPECT_EQ(h.eval("\"  x \".trim"), "x");
    EXPECT_EQ(h.eval("\"hello\".indexOf(\"ll\")"), "2");
    EXPECT_EQ(h.eval("\"hello\".contains(\"ell\")"), "true");
    EXPECT_EQ(h.eval("\"42\".toInt + 1"), "43");
    EXPECT_EQ(h.eval("\"abc\".reverse"), "cba");
    EXPECT_EQ(h.eval("\"\".isEmpty"), "true");
    EXPECT_EQ(h.eval("\"año\".length"), "3");
    EXPECT_TRUE(isError(h.eval("\"x\".toInt"), "NumberFormatException"));
    EXPECT_TRUE(isError(h.eval("\"abc\".charAt(5)"), "StringIndexOutOfBoundsException"));
}

TEST(Primitives, ListMethodsAndReentry) {
    EvalHarness h;
    EXPECT_EQ(h.eval("def l(xs: Int*) = xs"), "");
    EXPECT_EQ(h.eval("l(1, 2, 3).length"), "3");
    EXPECT_EQ(h.eval("l(5, 6)(1)"), "6");
    EXPECT_EQ(h.eval("l(1, 2, 3).mkString(\"-\")"), "1-2-3");
    EXPECT_EQ(h.eval("l(1, 2, 3).mkString(\"[\", \",\", \"]\")"), "[1,2,3]");
    EXPECT_EQ(h.eval("l(4).head"), "4");
    EXPECT_TRUE(isError(h.eval("l().head"), "NoSuchElementException"));
    EXPECT_TRUE(isError(h.eval("l(1)(3)"), "IndexOutOfBoundsException"));
    EXPECT_EQ(h.eval("{ var s = 0; l(1, 2, 3).foreach(x => s += x); s }"), "6");
    // Nested re-entry: a primitive calls a lambda that calls a primitive.
    EXPECT_EQ(h.eval("{ var s = 0; l(1, 2).foreach(x => l(10, 20).foreach(y => s += x * y)); s }"),
              "90");
}

TEST(Primitives, FunctionApplyAndAny) {
    EvalHarness h;
    EXPECT_EQ(h.eval("((x: Int) => x * x).apply(7)"), "49");
    EXPECT_EQ(h.eval("\"a\".equals(\"a\")"), "true");
    EXPECT_EQ(h.eval("(1 == 1).toString"), "true");
    EXPECT_EQ(h.eval("1.foo"), "error: NoSuchMethodError: value foo is not a member of Int");
}

TEST(Primitives, RestOfTheSurface) {
    EvalHarness h;
    // Int
    EXPECT_EQ(h.eval("7.+(2)"), "9");
    EXPECT_EQ(h.eval("7 / 2.0"), "3.5");
    EXPECT_EQ(h.eval("-7 % 2.5"), "-2.0");
    EXPECT_EQ(h.eval("3.min(-1)"), "-1");
    EXPECT_EQ(h.eval("3.max(7.5)"), "7.5");
    EXPECT_EQ(h.eval("~5"), "-6");
    EXPECT_EQ(h.eval("6 | 3"), "7");
    EXPECT_EQ(h.eval("6 ^ 3"), "5");
    EXPECT_EQ(h.eval("-16 >> 2"), "-4");
    EXPECT_EQ(h.eval("(1 << 70).toDouble"), "1.1805916207174113E21");
    EXPECT_EQ(h.eval("5.toLong"), "5");
    EXPECT_EQ(h.eval("1 % 0"), "error: ArithmeticException: / by zero");
    EXPECT_TRUE(isError(h.eval("1 << -1"), "IllegalArgumentException"));
    EXPECT_TRUE(isError(h.eval("(-1).toChar"), "IllegalArgumentException"));
    EXPECT_TRUE(isError(h.eval("1 & 2.0"), "ClassCastException"));
    // Double
    EXPECT_EQ(h.eval("-2.5.round"), "-2");
    EXPECT_EQ(h.eval("2.1.ceil"), "3.0");
    EXPECT_EQ(h.eval("(1.0 / 0).isInfinite"), "true");
    EXPECT_EQ(h.eval("(0.0 / 0.0).toInt"), "0");
    EXPECT_EQ(h.eval("1e20.toLong"), "100000000000000000000");
    EXPECT_EQ(h.eval("1.5.max(0.0 / 0.0).isNaN"), "true");
    EXPECT_EQ(h.eval("-1.5.abs"), "1.5");
    EXPECT_EQ(h.eval("2.0 < 3"), "true");
    EXPECT_TRUE(isError(h.eval("(1.0 / 0).toInt"), "ArithmeticException"));
    // Boolean and Char
    EXPECT_EQ(h.eval("false | true"), "true");
    EXPECT_EQ(h.eval("'a'.+(1)"), "98");
    EXPECT_EQ(h.eval("'a' < 'b'"), "true");
    EXPECT_EQ(h.eval("'Q'.toLower"), "q");
    EXPECT_EQ(h.eval("' '.isWhitespace"), "true");
    EXPECT_EQ(h.eval("'x'.isUpper"), "false");
    EXPECT_EQ(h.eval("'a'.toDouble"), "97.0");
    // String
    EXPECT_EQ(h.eval("\"año\".reverse"), "oña");
    EXPECT_EQ(h.eval("\"año\".charAt(2)"), "o");
    EXPECT_EQ(h.eval("\"año\".indexOf('o')"), "2");
    EXPECT_EQ(h.eval("\"abcabc\".indexOf(\"b\", 2)"), "4");
    EXPECT_EQ(h.eval("\"abc\".indexOf(\"z\")"), "-1");
    EXPECT_EQ(h.eval("\"Hello\".toLowerCase"), "hello");
    EXPECT_EQ(h.eval("\"hello\".startsWith(\"he\") && \"hello\".endsWith(\"lo\")"), "true");
    EXPECT_EQ(h.eval("\"a\".concat(\"b\")"), "ab");
    EXPECT_EQ(h.eval("\"a\".+(1)"), "a1");
    EXPECT_EQ(h.eval("\"ab\" * 0"), "");
    EXPECT_EQ(h.eval("\" 2.5e1 \".toDouble"), "25.0");
    EXPECT_EQ(h.eval("\"-12345678901234567890\".toInt"), "-12345678901234567890");
    EXPECT_EQ(h.eval("\"x\".nonEmpty"), "true");
    EXPECT_TRUE(isError(h.eval("\"1.5x\".toDouble"), "NumberFormatException"));
    EXPECT_TRUE(isError(h.eval("\"-\".toInt"), "NumberFormatException"));
    EXPECT_TRUE(isError(h.eval("\"abc\".substring(2, 1)"), "StringIndexOutOfBoundsException"));
    // List, Any, globals
    EXPECT_EQ(h.eval("def l(xs: Int*) = xs"), "");
    EXPECT_EQ(h.eval("l(1, 2).mkString"), "12");
    EXPECT_EQ(h.eval("l(1, 2).size"), "2");
    EXPECT_EQ(h.eval("l().isEmpty"), "true");
    EXPECT_EQ(h.eval("l(1).nonEmpty"), "true");
    EXPECT_EQ(h.eval("\"a\".!=(\"b\")"), "true");
    EXPECT_EQ(h.eval("{ val s = \"a\"; s.eq(s) }"), "true");
    EXPECT_EQ(h.eval("l(1).ne(l(1))"), "true");
    testing::internal::CaptureStdout();
    EXPECT_EQ(h.eval("println.apply(\"x\")"), "()");
    EXPECT_EQ(h.eval("{ print(1); println(); println(l(1, 2)); print(2.5) }"), "()");
    EXPECT_EQ(testing::internal::GetCapturedStdout(), "x\n1\nList(1, 2)\n2.5");
    EXPECT_TRUE(isError(h.eval("println(1, 2)"), "IllegalArgumentException"));
}

TEST(Primitives, NumericPlusStringAndLargeIntegerDivision) {
    EvalHarness h;
    // A numeric receiver's `+` concatenates a String argument (dot call: no fast path).
    EXPECT_EQ(h.eval("(1).+(\"a\")"), "1a");
    EXPECT_EQ(h.eval("'a'.+(\"b\")"), "ab");
    EXPECT_EQ(h.eval("2.5.+(\"x\")"), "2.5x");
    // `/` truncates toward zero and `%` takes the dividend's sign, beyond 64 bits too.
    EXPECT_EQ(h.eval("(1 << 70) / 3"), "393530540239137101141");
    EXPECT_EQ(h.eval("(1 << 70) % 7"), "2");
    EXPECT_EQ(h.eval("-(1 << 70) / 3"), "-393530540239137101141");
    EXPECT_EQ(h.eval("-(1 << 70) % 7"), "-2");
    EXPECT_EQ(h.eval("(1 << 70) % -7"), "2");
    EXPECT_EQ(h.eval("7 / (1 << 70)"), "0");
    EXPECT_EQ(h.eval("-7 % (1 << 70)"), "-7");
    EXPECT_EQ(h.eval("(1 << 70) / 0"), "error: ArithmeticException: / by zero");
    EXPECT_EQ(h.eval("(1 << 70) % 0"), "error: ArithmeticException: / by zero");
}

TEST(Primitives, ArgumentCountErrorsNameTheCalledMethod) {
    EvalHarness h;
    const std::string prefix = "error: IllegalArgumentException: ";
    EXPECT_EQ(h.eval("1.toLong(2)"), prefix + "toLong takes 0 argument(s), got 1");
    EXPECT_EQ(h.eval("1.toFloat(2)"), prefix + "toFloat takes 0 argument(s), got 1");
    EXPECT_EQ(h.eval("1.5.toLong(2)"), prefix + "toLong takes 0 argument(s), got 1");
    EXPECT_EQ(h.eval("1.5.toFloat(2)"), prefix + "toFloat takes 0 argument(s), got 1");
    EXPECT_EQ(h.eval("'a'.toLong(2)"), prefix + "toLong takes 0 argument(s), got 1");
    EXPECT_EQ(h.eval("'a'.toChar(2)"), prefix + "toChar takes 0 argument(s), got 1");
    EXPECT_EQ(h.eval("def l(xs: Int*) = xs"), "");
    EXPECT_EQ(h.eval("l(1).size(2)"), prefix + "size takes 0 argument(s), got 1");
}

TEST(Primitives, TuplesAreCaseClassesNeverProtoTuples) {
    EvalHarness h;
    EXPECT_EQ(h.eval("(1, \"a\")"), "(1,a)");
    EXPECT_EQ(h.eval("new Tuple2(1, 2) == (1, 2)"), "true");
    EXPECT_EQ(h.eval("(1, 2).copy(_2 = 5)"), "(1,5)");
    EXPECT_EQ(h.eval("(1, 2).hashCode"), "1316541600");
}
