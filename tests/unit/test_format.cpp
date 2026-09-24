/*
 * FormatSpec — the f-interpolator specifier parser (Phase 3, A0-2, D55).
 *
 * These cases test the parser alone, without protoCore: the compiler depends on
 * exactly this, because a malformed specifier must be rejected at compile time
 * with the interpolation's source position.
 */
#include "support/FormatSpec.h"

#include <gtest/gtest.h>

#include <stdexcept>

using namespace protoScala;

TEST(FormatSpec, ParsesFlagsWidthAndPrecision) {
    // Java's order: %[flags][width][.precision]conversion.
    const FormatSpec s = parseFormatSpec("%-+,08.3f");
    EXPECT_TRUE(s.leftAlign);
    EXPECT_TRUE(s.plusSign);
    EXPECT_TRUE(s.zeroPad);
    EXPECT_TRUE(s.grouping);
    EXPECT_EQ(s.width, 8);
    EXPECT_EQ(s.precision, 3);
    EXPECT_EQ(s.conversion, 'f');
}

TEST(FormatSpec, AnEmptySpecIsPercentS) {
    const FormatSpec s = parseFormatSpec("");
    EXPECT_EQ(s.conversion, 's');
    EXPECT_EQ(s.width, -1);
    EXPECT_EQ(s.precision, -1);
}

TEST(FormatSpec, WidthWithoutPrecisionAndPrecisionWithoutWidth) {
    EXPECT_EQ(parseFormatSpec("%10s").width, 10);
    EXPECT_EQ(parseFormatSpec("%10s").precision, -1);
    EXPECT_EQ(parseFormatSpec("%.2f").width, -1);
    EXPECT_EQ(parseFormatSpec("%.2f").precision, 2);
    EXPECT_EQ(parseFormatSpec("%.f").precision, 0);   // "%.f" is precision 0
}

TEST(FormatSpec, EveryDocumentedConversionIsAccepted) {
    for (const char* spec : {"%s", "%b", "%c", "%d", "%o", "%x", "%X",
                             "%e", "%E", "%f", "%g", "%G"})
        EXPECT_NO_THROW(parseFormatSpec(spec)) << spec;
}

TEST(FormatSpec, RejectsAnUnknownConversion) {
    EXPECT_THROW(parseFormatSpec("%q"), std::invalid_argument);
    EXPECT_THROW(parseFormatSpec("%5"), std::invalid_argument);
    EXPECT_THROW(parseFormatSpec("%n"), std::invalid_argument);  // D55: write \n
    EXPECT_THROW(parseFormatSpec("%dd"), std::invalid_argument);
    EXPECT_THROW(parseFormatSpec("d"), std::invalid_argument);   // no leading %
}
