#include "frontend/Linearizer.h"

#include <gtest/gtest.h>

#include <map>
#include <string>
#include <vector>

using protoScala::linearize;

namespace {

// A tiny hierarchy database: every type's parents in declaration order
// (superclass first). Linearizations are computed bottom-up like the compiler does.
class Hierarchy {
public:
    void add(const std::string& name, std::vector<std::string> parents = {}) {
        parents_[name] = std::move(parents);
    }
    std::vector<std::string> lin(const std::string& name) {
        if (name == "AnyRef") return {"AnyRef", "Any"};
        std::vector<std::vector<std::string>> ps;
        for (const auto& p : parents_.at(name)) ps.push_back(lin(p));
        if (ps.empty()) ps.push_back({"AnyRef", "Any"});
        return linearize(name, ps);
    }
    std::string text(const std::string& name) {
        std::string out;
        for (const auto& n : lin(name)) out += (out.empty() ? "" : ", ") + n;
        return out;
    }

private:
    std::map<std::string, std::vector<std::string>> parents_;
};

} // namespace

TEST(Linearizer, ScalaSpecificationIterExample) {
    // SLS 5.1.2: class Iter extends StringIterator with RichIterator
    Hierarchy h;
    h.add("AbsIterator");
    h.add("RichIterator", {"AbsIterator"});
    h.add("StringIterator", {"AbsIterator"});
    h.add("Iter", {"StringIterator", "RichIterator"});
    EXPECT_EQ(h.text("Iter"), "Iter, RichIterator, StringIterator, AbsIterator, AnyRef, Any");
}

TEST(Linearizer, ProgrammingInScalaCat) {
    Hierarchy h;
    h.add("Animal");
    h.add("Furry", {"Animal"});
    h.add("HasLegs", {"Animal"});
    h.add("FourLegged", {"HasLegs"});
    h.add("Cat", {"Animal", "Furry", "FourLegged"});
    EXPECT_EQ(h.text("Cat"), "Cat, FourLegged, HasLegs, Furry, Animal, AnyRef, Any");
}

TEST(Linearizer, DesignDocumentExample) {
    // DESIGN §4.3: class C extends B with T1 with T2
    Hierarchy h;
    h.add("B");
    h.add("T1");
    h.add("T2");
    h.add("C", {"B", "T1", "T2"});
    EXPECT_EQ(h.text("C"), "C, T2, T1, B, AnyRef, Any");
}

TEST(Linearizer, DiamondKeepsTheLastOccurrence) {
    Hierarchy h;
    h.add("A");
    h.add("B", {"A"});
    h.add("C", {"A"});
    h.add("D", {"B", "C"});
    EXPECT_EQ(h.text("D"), "D, C, B, A, AnyRef, Any");
}

TEST(Linearizer, StackableTraitsQueue) {
    // Programming in Scala ch. 12: class MyQueue extends BasicIntQueue with Incrementing with Filtering
    Hierarchy h;
    h.add("IntQueue");
    h.add("BasicIntQueue", {"IntQueue"});
    h.add("Doubling", {"IntQueue"});
    h.add("Incrementing", {"IntQueue"});
    h.add("Filtering", {"IntQueue"});
    h.add("MyQueue", {"BasicIntQueue", "Incrementing", "Filtering"});
    EXPECT_EQ(h.text("MyQueue"), "MyQueue, Filtering, Incrementing, BasicIntQueue, IntQueue, AnyRef, Any");
    h.add("Q2", {"BasicIntQueue", "Filtering", "Incrementing"});
    EXPECT_EQ(h.text("Q2"), "Q2, Incrementing, Filtering, BasicIntQueue, IntQueue, AnyRef, Any");
}

TEST(Linearizer, TraitOnlyParentsAndDeepChains) {
    Hierarchy h;
    h.add("T");
    h.add("C", {"T"});  // class C extends T: the superclass is AnyRef
    EXPECT_EQ(h.text("C"), "C, T, AnyRef, Any");
    h.add("L0");
    for (int k = 1; k <= 12; ++k) h.add("L" + std::to_string(k), {"L" + std::to_string(k - 1)});
    const auto l = h.lin("L12");
    ASSERT_EQ(l.size(), 15u);
    EXPECT_EQ(l.front(), "L12");
    EXPECT_EQ(l[12], "L0");
}
