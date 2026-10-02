/*
 * CppTables — one table emitter, two users (Phase 7 §D1(d), Task 5).
 *
 * `protoscala-precompile` writes the prelude image and `protoscalac` writes a
 * transpiled module. Both turn a `BytecodeModule` tree into static C++ tables,
 * and both need the same three primitives: one string escaper, one exact
 * `double` literal, and one depth-first flattening whose order is the order a
 * `MAKE_FN` operand means. Two escapers that disagree about one byte is a class
 * of bug with no symptom until a string literal is wrong, so there is one.
 *
 * What differs between the two users is the RECORD TYPES, not the mechanics: the
 * prelude image writes `PreludeConstRec` and friends, which
 * `src/runtime/PreludeImage.cpp` walks to rebuild `BytecodeModule`s; a transpiled
 * module writes `protoScala::gen::ConstRec` and friends, which are the installed
 * ABI and are read in place. The emitters below serve the second; the first keeps
 * its own writers and shares the primitives.
 */
#pragma once
#include "compiler/BytecodeModule.h"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>
#include <cstddef>
#include <vector>

namespace protoScala::tables {

/**
 * Writes `s` as a C++ string literal that round-trips every byte. A `?` is
 * escaped so the output can never start a trigraph, and a byte outside printable
 * ASCII is written as a `"" \xNN ""` splice so it cannot absorb the digits that
 * follow it.
 */
void emitString(std::ostream& o, std::string_view s);

/** `emitString` into a string. */
std::string quoted(std::string_view s);

/**
 * The longest string written as a literal into a generated table. MSVC caps one
 * string literal at 16380 bytes and a concatenation of literals at 65535, so a
 * longer string (a long Scala constant, a long pool entry) is written as a byte
 * array instead -- on every platform, so the generated file does not depend on
 * the compiler that will build it.
 */
inline constexpr std::size_t kMaxStringLiteralBytes = 4096;

/**
 * An expression for `s` that a table entry of type `const char*` can hold: the
 * quoted literal when `s` fits kMaxStringLiteralBytes, otherwise the name of a
 * NUL-terminated `static const char <arrayName>[]` that is written to `defs`
 * (which must precede the table that uses it).
 */
std::string stringRef(std::ostream& defs, std::string_view s, const std::string& arrayName);

/**
 * A `double` as a C++ literal that round-trips bit for bit. A hexadecimal
 * floating literal (C++17) is exact; a non-finite value would not be, so it is
 * refused rather than silently approximated. `where` names the input in the
 * message.
 */
std::string exactDouble(double d, std::string_view where = "the input");

/**
 * One block of a flattened `BytecodeModule` tree. `index` 0 is the root and
 * `parent` is `-1` there.
 *
 * **The order is the order a `MAKE_FN` operand means**, which is the order
 * `BytecodeModule::addBlock` created — depth-first, each child immediately after
 * its parent. `flatten` checks that invariant rather than assuming it.
 */
struct FlatBlock {
    const BytecodeModule* mod;
    int parent;
    std::string cppName;   // "blk<index>"
};

/** Depth-first; throws `std::logic_error` if a block's index does not round-trip. */
std::vector<FlatBlock> flatten(const BytecodeModule& root);

/**
 * A per-block pool of the strings a generated block's tables point into: a
 * `Names` constant's members, a `ClassSpec`'s member keys and fields, a
 * `KwSendSite`'s keywords. Ranges are appended, never de-duplicated, so a
 * `[first, first + count)` slice is always contiguous.
 */
class StringPool {
public:
    /** Appends `v` and returns its first index. */
    int add(const std::vector<std::string>& v);
    const std::vector<std::string>& all() const { return pool_; }
    std::size_t size() const { return pool_.size(); }

private:
    std::vector<std::string> pool_;
};

/**
 * Writes `static const protoScala::gen::ConstRec <name>[] = { ... };` — one
 * record per `mod.constAt(i)`, in pool order, with every name appended to `pool`.
 * A zero-length C array is not valid C++, so an empty pool emits one filler
 * record and the count stays 0.
 */
void emitConsts(std::ostream& o, const std::string& name, const BytecodeModule& mod,
                StringPool& pool);

/**
 * Writes `static const protoScala::gen::HandlerRec <name>[] = { ... };` **in
 * table order**, because the compiler appends nested `try`s before enclosing ones
 * and a `try`'s `Catch` entry before its `Finally` entry: table order IS search
 * order, and `gen::handlerFor` relies on it exactly as `BytecodeModule::handlerFor`
 * does.
 */
void emitHandlers(std::ostream& o, const std::string& name, const BytecodeModule& mod);

/** Writes `static const char* const <name>[] = { ... };` for a string pool. */
void emitStrings(std::ostream& o, const std::string& name, const StringPool& pool);

/**
 * Writes the three symbol arrays a block fills once at load time
 * (`<name>_symbols`, `<name>_keySymbols`, `<name>_stringSymbols`).
 *
 * They hold `createSymbol` symbols, which are strong and never collected, so an
 * array is not a GC root and needs none — the same reasoning
 * `BytecodeModule`'s P1-boundary comment already records.
 */
void emitSymbolArrays(std::ostream& o, const std::string& name, std::size_t constCount,
                      std::size_t stringCount);

/** Writes `static const protoScala::gen::BlockRec <name>_rec = { ... };`. */
void emitBlockRec(std::ostream& o, const FlatBlock& blk, std::size_t index,
                  const std::vector<std::size_t>& childIndices, std::size_t stringCount);

}  // namespace protoScala::tables
