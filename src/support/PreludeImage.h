/*
 * PreludeImage — the compiled prelude as a BUILD PRODUCT, not a cache.
 *
 * `PreludeImage.cpp` is generated at build time by `protoscala-precompile` from
 * lib/prelude.scala, through an add_custom_command that DEPENDS on that file.
 * There is no cache directory, no mtime comparison and nothing a user can
 * invalidate: the image is regenerated whenever the source changes, in the same
 * way PreludeSource.cpp already is.
 *
 * Two guards catch a hand-copied or hand-edited image, because a stale prelude
 * would be silently wrong rather than loudly broken:
 *   - kPreludeImageFormat must equal the version this binary was compiled with;
 *   - preludeImageSourceHash() must equal preludeSourceHash(), the FNV-1a-64 of
 *     the prelude source embedded in the same binary.
 * On either mismatch loadPrelude prints one line to stderr and compiles the
 * embedded source instead, so the binary is always correct and only slower.
 *
 * The image holds no ProtoObject* and no address: it holds strings, integers
 * and PODs, and buildPreludeImage CONSTRUCTS BytecodeModule objects from them.
 * linkSymbols still runs afterwards, exactly as on the source path, which is
 * what keeps the image independent of any ProtoSpace.
 *
 * Division of labour (a deliberate departure from the plan's sketch, recorded in
 * DECISIONS-LOG as agent decision A1): the GENERATED file contains tables only,
 * exposed through `preludeImageData()`. The reconstruction that walks them is a
 * hand-written, reviewed source file (src/runtime/PreludeImage.cpp) that is not
 * generated at all — which serves the plan's stated intent ("it cannot drift per
 * prelude") more strongly than copying fixed code into generator output, and
 * keeps the logic in the repository where it can be read and diffed.
 */
#pragma once
#include "compiler/BytecodeModule.h"
#include "compiler/GlobalTable.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace protoScala {

// Bumped by hand whenever the record layouts below change shape. A binary
// refuses an image that does not match, and falls back to the source.
inline constexpr std::uint32_t kPreludeImageFormat = 1;

// --- The generated tables -------------------------------------------------
// Every `*First`/`*Count` pair is a half-open range into the pool named in the
// comment. Strings are C strings in one pool, so a record never owns storage.

// One entry of a BytecodeModule constant pool, in the order the compiler
// created it. The image REPLAYS the add* calls rather than assigning slots, so
// the pool's de-duplication produces byte-identical indices.
struct PreludeConstRec {
    std::uint8_t kind = 0;      // BytecodeModule::ConstKind
    long long ival = 0;         // Int; Char (code point)
    double dval = 0.0;          // Double
    int base = 10;              // BigInt
    std::uint32_t argc = 0;     // SendSite/SuperSite argc; KwSendSite positional;
                                // ClassSpec parent count
    std::uint32_t flags = 0;    // ClassSpec flag bits
    bool exact = false;         // SuperSite: super[T].m
    const char* sval = nullptr; // String bytes; BigInt digits; a site's name
    const char* key = nullptr;  // ClassSpec type key; SuperSite owner key;
                                // SendSite/KwSendSite fallback name (D5)
    int namesFirst = 0, namesCount = 0;    // Names; ClassSpec members; KwSendSite keywords
    int fieldsFirst = 0, fieldsCount = 0;  // ClassSpec product elements
};

struct PreludeHandlerRec {
    std::uint32_t startPc = 0, endPc = 0, handlerPc = 0;
    int stackDepth = 0, slot = 0;
    std::uint8_t kind = 0;  // BytecodeModule::HandlerKind
};

struct PreludeCaptureRec {
    int parentSlot = 0, localSlot = 0;
};

// One BytecodeModule. The tree is flattened depth-first; `parent` is the index
// of the enclosing module, -1 for the top level. Reconstruction adds blocks in
// index order, so every block index baked into a MAKE_FN operand stays exactly
// what the compiler emitted.
struct PreludeModuleRec {
    int parent = -1;
    const char* name = nullptr;
    int arity = 0;
    bool variadic = false;
    bool method = false;
    bool paramless = false;
    int localCount = 0, maxStack = 0;
    int codeFirst = 0, codeCount = 0;
    int constFirst = 0, constCount = 0;
    int handlerFirst = 0, handlerCount = 0;
    int captureFirst = 0, captureCount = 0;
    int paramFirst = 0, paramCount = 0;        // into the string pool
    int defaultFirst = 0, defaultCount = 0;    // into the default-block pool
};

// A `setDefaultBlock(param, block)` call, replayed in declaration order.
struct PreludeDefaultRec {
    std::uint32_t param = 0, block = 0;
};

struct PreludeBindingRec {
    const char* name = nullptr;
    std::uint8_t kind = 0;  // BindingKind
    const char* key = nullptr;
    int maskFirst = 0, maskCount = 0;  // into the mask pool
};

struct PreludeMemberRec {
    const char* name = nullptr;
    std::uint8_t kind = 0;  // MemberKind
    const char* key = nullptr;
    bool concrete = true;
    bool byNameValue = false;
    int maskFirst = 0, maskCount = 0;
};

// ClassInfo boolean fields, packed so the record stays readable.
enum PreludeTypeFlag : std::uint32_t {
    kPTCase = 1u << 0,
    kPTAbstract = 1u << 1,
    kPTFinal = 1u << 2,
    kPTSealed = 1u << 3,
    kPTBuiltin = 1u << 4,
    kPTMutableInstances = 1u << 5,
    kPTHasInit = 1u << 6,
    kPTPrimaryVariadic = 1u << 7,
    kPTCompanionHasApply = 1u << 8,
    kPTCompanionHasUnapply = 1u << 9,
};

struct PreludeTypeRec {
    const char* name = nullptr;
    const char* key = nullptr;
    std::uint8_t kind = 0;  // ClassKind
    std::uint32_t flags = 0;
    int linFirst = 0, linCount = 0;          // into the string pool
    int fieldFirst = 0, fieldCount = 0;      // into the string pool
    int ctorFirst = 0, ctorCount = 0;        // into the string pool
    int auxFirst = 0, auxCount = 0;          // into the aux-arity pool
    int primaryMaskFirst = 0, primaryMaskCount = 0;  // into the mask pool
    int memberFirst = 0, memberCount = 0;    // into the member pool
    std::uint32_t primaryArity = 0, primaryMinArity = 0;
    const char* companionTermKey = nullptr;
    const char* companionTypeKey = nullptr;
};

// One entry of the type NAMESPACE: the name a source spelling resolves under,
// and the type key it resolves to. Kept separate from PreludeTypeRec because the
// two are not the same thing — `object Try`'s ClassInfo is named `Try` while its
// namespace entry is `Try.type` — and collapsing them silently renamed every
// companion object's class.
struct PreludeTypeAliasRec {
    const char* name = nullptr;
    const char* key = nullptr;
};

// One entry of the conservative by-name selector index (CaptureAnalysis's
// union). Without it a call to a prelude `def` with a by-name parameter would be
// under-boxed, and a missing Cell is not harmless where an extra one is.
struct PreludeSelectorRec {
    const char* name = nullptr;
    std::uint32_t mask = 0;
};

// Everything the generated translation unit exposes. A plain aggregate of
// spans: no ownership, no allocation, no ProtoObject*.
struct PreludeImageData {
    std::uint32_t format = 0;
    std::uint64_t sourceHash = 0;
    const std::uint32_t* code = nullptr;  // Instr words
    std::size_t codeCount = 0;
    const int* lines = nullptr;
    std::size_t lineCount = 0;
    const char* const* strings = nullptr;
    std::size_t stringCount = 0;
    const std::uint32_t* masks = nullptr;
    std::size_t maskCount = 0;
    const std::uint32_t* auxArities = nullptr;
    std::size_t auxArityCount = 0;
    const PreludeConstRec* consts = nullptr;
    std::size_t constCount = 0;
    const PreludeHandlerRec* handlers = nullptr;
    std::size_t handlerCount = 0;
    const PreludeCaptureRec* captures = nullptr;
    std::size_t captureCount = 0;
    const PreludeDefaultRec* defaults = nullptr;
    std::size_t defaultCount = 0;
    const PreludeModuleRec* modules = nullptr;
    std::size_t moduleCount = 0;
    const PreludeBindingRec* bindings = nullptr;
    std::size_t bindingCount = 0;
    const PreludeMemberRec* members = nullptr;
    std::size_t memberCount = 0;
    const PreludeTypeRec* types = nullptr;
    std::size_t typeCount = 0;
    const PreludeTypeAliasRec* typeAliases = nullptr;
    std::size_t typeAliasCount = 0;
    const PreludeSelectorRec* selectors = nullptr;
    std::size_t selectorCount = 0;
};

// Present only when an image was generated (absent when cross-compiling).
#if defined(PROTOSCALA_HAVE_PRELUDE_IMAGE)
// Defined by the GENERATED PreludeImage.cpp.
const PreludeImageData& preludeImageData();

// Rebuilds the compiled prelude: appends the top-level module (and its blocks)
// to `modules` and fills `globals` with exactly the bindings and ClassInfos the
// compiler produced. The returned module is NOT linked; the caller calls
// linkSymbols on it. Defined by the hand-written src/runtime/PreludeImage.cpp.
const BytecodeModule& buildPreludeImage(GlobalTable& globals,
                                        std::vector<std::unique_ptr<BytecodeModule>>& modules);
#endif

// FNV-1a-64 of the prelude source embedded in this binary. Computed once,
// lazily, over ~10 KB. (Defined by the generated PreludeSource.cpp.)
std::uint64_t preludeSourceHash();

// The same hash over an arbitrary buffer, so the generator and the runtime
// cannot disagree about the algorithm.
constexpr std::uint64_t fnv1a64(const char* p, std::size_t n) {
    std::uint64_t acc = 0xcbf29ce484222325ull;
    for (std::size_t i = 0; i < n; ++i) {
        acc ^= static_cast<std::uint64_t>(static_cast<unsigned char>(p[i]));
        acc *= 0x100000001b3ull;
    }
    return acc;
}

} // namespace protoScala
