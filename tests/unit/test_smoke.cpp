// Smoke tests: the version is wired from CMake, and protoCore links and runs.
#include "protoScala/Version.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <string>

TEST(Smoke, VersionMatchesComponents) {
    const std::string expected =
        std::to_string(protoScala::kVersionMajor) + "." +
        std::to_string(protoScala::kVersionMinor) + "." +
        std::to_string(protoScala::kVersionPatch);
    EXPECT_EQ(expected, protoScala::versionString());
}

TEST(Smoke, ProtoCoreSmallIntegerRoundTrip) {
    proto::ProtoSpace space;
    proto::ProtoContext* ctx = space.rootContext;

    const proto::ProtoObject* n = ctx->fromInteger(42);
    ASSERT_TRUE(proto::isSmallInt(n));
    EXPECT_EQ(42, proto::asSmallInt(n));

    // 2^53 is one past the SmallInteger range: protoCore must promote it.
    const proto::ProtoObject* big = ctx->fromInteger(1LL << 53);
    EXPECT_FALSE(proto::isSmallInt(big));
    EXPECT_EQ(1LL << 53, big->asLong(ctx));
}
