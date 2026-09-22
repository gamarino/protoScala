#include "runtime/Hashing.h"

#include <gtest/gtest.h>

using namespace protoScala::hashing;

TEST(Hashing, JavaStringHashCode) {
    EXPECT_EQ(javaStringHash("abc"), 96354);
    EXPECT_EQ(javaStringHash("Unique"), -1756661775);
    EXPECT_EQ(javaStringHash("Empty"), 67081517);
    EXPECT_EQ(javaStringHash(""), 0);
    EXPECT_EQ(javaStringHash("\xF0\x9F\x98\x80"), 1772899);  // U+1F600: two UTF-16 units
}

TEST(Hashing, CaseClassAndTupleHashCodesMatchScala) {
    EXPECT_EQ(productHash(javaStringHash("Point"), {1, 2}), -694993394);
    EXPECT_EQ(productHash(javaStringHash("Box"), {javaStringHash("abc")}), -1480185351);
    EXPECT_EQ(productHash(javaStringHash("Tuple2"), {1, javaStringHash("a")}), 1971805870);
    EXPECT_EQ(productHash(javaStringHash("Tuple2"), {1, 2}), 1316541600);
    EXPECT_EQ(productHash(javaStringHash("Empty"), {}), 67081517);  // arity 0: the prefix's hash
}

TEST(Hashing, NumbersFollowStatics) {
    EXPECT_EQ(longHash(42), 42);
    EXPECT_EQ(longHash(-1), -1);
    EXPECT_EQ(longHash(1LL << 40), 256);          // (int)(v ^ (v >>> 32))
    EXPECT_EQ(javaDoubleHash(1.5), 1073217536);   // 1.5.hashCode
    EXPECT_EQ(doubleHash(2.0), 2);                // a whole Double hashes as the Int
    EXPECT_EQ(doubleHash(1.5), javaFloatHash(1.5f));
    EXPECT_EQ(doubleHash(-0.0), 0);
}
