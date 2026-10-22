// The Scala-name-to-Smalltalk-selector rule (src/umd/SmalltalkSelector.h, D135),
// one case per clause, and each refusal. The rule is a pure function, so it is
// pinned here rather than only through protoST programs.
#include "umd/SmalltalkSelector.h"

#include <gtest/gtest.h>

using protoScala::smalltalkSelector;

namespace {
std::string sel(const char* name, unsigned argc) {
    std::string why;
    auto s = smalltalkSelector(name, argc, &why);
    return s ? *s : "refused: " + why;
}
}  // namespace

TEST(SmalltalkSelector, AnExplicitSelectorIsKeptWithOneColonPerArgument) {
    EXPECT_EQ(sel("at:put:", 2), "at:put:");
    EXPECT_EQ(sel("value:", 1), "value:");
    EXPECT_NE(sel("at:put:", 1).find("refused: the Smalltalk selector at:put: takes 2"),
              std::string::npos);
}

TEST(SmalltalkSelector, AnOperatorIsABinarySelector) {
    EXPECT_EQ(sel("+", 1), "+");
    EXPECT_EQ(sel("<=", 1), "<=");
    EXPECT_NE(sel("+", 2).find("refused"), std::string::npos);
}

TEST(SmalltalkSelector, NoArgumentIsUnary) {
    EXPECT_EQ(sel("increment", 0), "increment");
    EXPECT_EQ(sel("print_string", 0), "print_string");  // a unary name keeps its '_'
}

TEST(SmalltalkSelector, OnePartPerArgumentGivesAKeywordSelector) {
    EXPECT_EQ(sel("at_put", 2), "at:put:");
    EXPECT_EQ(sel("inject_into", 2), "inject:into:");
    EXPECT_EQ(sel("transfer_to", 2), "transfer:to:");
    EXPECT_EQ(sel("add", 1), "add:");
}

TEST(SmalltalkSelector, OtherwiseOneArgumentTakesOneColon) {
    EXPECT_EQ(sel("add_item", 1), "add_item:");
    EXPECT_EQ(sel("_private", 1), "_private:");
}

TEST(SmalltalkSelector, AnythingElseIsRefusedNamingTheBacktickForm) {
    const std::string r = sel("put", 2);
    EXPECT_NE(r.find("refused: cannot map 'put' with 2 arguments"), std::string::npos) << r;
    EXPECT_NE(r.find("backticks"), std::string::npos) << r;
    EXPECT_NE(sel("at__put", 2).find("refused"), std::string::npos);  // an empty part
    EXPECT_NE(sel("", 0).find("refused"), std::string::npos);
}
