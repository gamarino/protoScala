/*
 * GeneratedModule.h — the whole published C++ surface of protoScala.
 *
 * **This file is the whole published surface: a generated module includes this
 * and `protoCore.h`, nothing else.** Nothing is added to it without an ABI note
 * in `docs/PROTOSCALAC_SPECIFICATION.md`, and `PROTOSCALA_ABI_SOVERSION` moves
 * when this file changes incompatibly, and only then. Keeping the surface to one
 * file is what makes the ABI bounded, reviewable in one diff, and unable to grow
 * by accident (Phase 7 §D3).
 *
 * Everything here is implemented in `libprotoScala.so`, in
 * `src/runtime/GeneratedSupport.cpp`, and every operation forwards to the same
 * `inline` function `ExecutionEngine::runLoop` calls
 * (`src/runtime/OpcodeOps.h`). There is no second implementation of any opcode.
 *
 * Two rules a generated file must obey, both structural rather than advisory:
 *
 *  - **P1: no generated C++ local ever holds a `const proto::ProtoObject*`.**
 *    Every value lives in a `Frame` slot — the parameters, the locals, the
 *    operand stack and the reserved in-flight-exception slot are all traced
 *    automatic locals of one `ProtoContext`. A function here may allocate, so a
 *    value held only in a C++ local across a call is invisible to the collector.
 *  - **P2: one `ProtoContext` per invocation.** Each generated block opens
 *    exactly one `Frame`, which owns that context.
 *
 * protoScala has no `getCurrentContext()`: a `proto::ProtoContext*` is an
 * explicit parameter of every engine method and every native. `currentContext()`
 * below exists for exactly one caller, `proto_module_init`, which is entered
 * across a `dlopen` boundary with no context argument.
 */
#ifndef PROTOSCALA_GENERATED_MODULE_H
#define PROTOSCALA_GENERATED_MODULE_H

#include <protoCore.h>

#include <cstddef>
#include <cstdint>

namespace protoScala::gen {

/** Bumped only when this header changes incompatibly; equals PROTOSCALA_ABI_SOVERSION. */
inline constexpr std::uint32_t kGeneratedModuleABI = 1;

// --- static tables the generated file defines ------------------------------

/**
 * One constant-pool entry, in the order the compiler created it.
 *
 * `sval` carries a length as well as a pointer, because a Scala `String` literal
 * may hold a NUL and `BytecodeModule::Const::sval` is a `std::string` for that
 * reason. A C string alone would silently truncate such a constant.
 */
struct ConstRec {
    std::uint8_t kind;          // BytecodeModule::ConstKind
    long long ival;             // Int; Char code point
    double dval;                // Double
    int base;                   // BigInt
    std::uint32_t argc;         // SendSite/SuperSite argc; KwSendSite positional; ClassSpec parents
    std::uint32_t flags;        // ClassSpec flag bits
    bool exact;                 // SuperSite: super[T].m
    const char* sval;           // String bytes; BigInt digits; a site's name
    std::size_t slen;           // length of sval in bytes (a String constant may hold NUL)
    const char* key;            // ClassSpec type key; SuperSite owner key; SendSite fallback (D5)
    int namesFirst, namesCount; // Names; ClassSpec members; KwSendSite keywords
    int fieldsFirst, fieldsCount;
};

struct HandlerRec {
    std::uint32_t startPc, endPc, handlerPc;
    int stackDepth, slot;
    std::uint8_t kind;          // BytecodeModule::HandlerKind
};

/** Everything one generated block needs that is not code. */
struct BlockRec {
    const char* name;
    std::uint32_t arity, localCount, maxStack, captureCount;
    bool variadic, method, paramless;
    /** This block's own thunk — the `proto::ProtoMethod` a caller reaches. */
    proto::ProtoMethod entry;
    const ConstRec* consts; std::size_t constCount;
    const HandlerRec* handlers; std::size_t handlerCount;
    const char* const* strings; std::size_t stringCount;
    /** Interned `sval` of each ConstRec that needs one; filled once by linkModule.
     *  A `nullptr` entry means that constant carries no name. */
    const proto::ProtoString** symbols;
    /** Interned `key` of each ConstRec that has one: a ClassSpec's type key, a
     *  SuperSite's owner key, a SendSite's D5 fallback name. */
    const proto::ProtoString** keySymbols;
    /** One interned symbol per `strings` entry, so a Names / ClassSpec-member /
     *  KwSendSite-keyword range resolves as `stringSymbols[namesFirst + k]`. */
    const proto::ProtoString** stringSymbols;
    /** Nested blocks, indexed by the operand MAKE_FN carries. */
    const proto::ProtoMethod* blocks; std::size_t blockCount;
};

// --- module lifecycle ------------------------------------------------------

/**
 * The context the caller is running in, from `activeCallContext()`. Throws
 * `std::logic_error` naming the caller when there is none: a generated module
 * reached outside a protoScala call context is a host defect, and D74 keeps a
 * defect uncatchable. protoScala has no `getCurrentContext()`; this is the one
 * place a generated file may obtain a context without being handed one.
 */
proto::ProtoContext* currentContext(const char* caller);

/** The () singleton of this space (RuntimeLayout::unit). */
const proto::ProtoObject* unitValue(proto::ProtoContext* ctx);

/**
 * Interns every symbol of `blocks[0..n)` in `ctx`'s space, exactly once per
 * space. This is what replaces `BytecodeModule::linkSymbols`. Idempotent; throws
 * `std::logic_error` if asked to link the same module into a second space,
 * because a module's symbols are one space's strong symbols.
 */
void linkModule(proto::ProtoContext* ctx, const BlockRec* const* blocks, std::size_t n);

/**
 * Script mode (§D8): looks up the `@main` global, calls it through
 * `ExecutionEngine::callTopLevel`, reports an uncaught Scala exception in the
 * same shape `Session::runScript` does, and returns the process exit code.
 */
int runMain(proto::ProtoContext* ctx, const char* mainKey, bool takesArgs, int argc, char** argv);

/**
 * §D6 site 2: the boundary every generated block's thunk presents to the VM.
 * Calls `body` inside `translateForeignException`, whose six clauses are in
 * `src/umd/ForeignBoundary.h`. The emitter writes the thunk; the clauses live
 * here, so a generator bug cannot drop the `std::logic_error` clause and retire
 * D74 in a file no human wrote.
 */
const proto::ProtoObject* enterMethod(proto::ProtoMethod body, proto::ProtoContext* ctx,
                                      const proto::ProtoObject* self, const proto::ParentLink* pl,
                                      const proto::ProtoList* args,
                                      const proto::ProtoSparseList* kwargs);

/**
 * Runs the module body through `translateForeignException` (§D6 site 1) and
 * returns the module object the provider publishes.
 */
const proto::ProtoObject* runModuleBody(proto::ProtoContext* ctx, proto::ProtoMethod body,
                                        const char* logicalPath, const char* version);

// --- the frame -------------------------------------------------------------

/**
 * One invocation's `ProtoContext` and traced slots (P1, P2). Reproduces
 * `ExecutionEngine::execute`'s prologue: `arity` parameter slots, then
 * `localCount` locals, then `maxStack` operand slots, then ONE reserved slot for
 * an in-flight exception value.
 */
class Frame {
public:
    Frame(proto::ProtoContext* parent, const BlockRec& blk, const proto::ProtoObject* self,
          const proto::ProtoList* args, const proto::ProtoSparseList* kwargs);
    ~Frame();
    Frame(const Frame&) = delete;
    Frame& operator=(const Frame&) = delete;

    proto::ProtoContext* ctx() { return ctx_; }
    const proto::ProtoObject** slots() { return slots_; }
    /** arity + localCount: where the operand stack starts. */
    unsigned stackBase() const { return stackBase_; }
    /** The reserved in-flight-exception slot index. */
    unsigned pendingSlot() const { return pendingSlot_; }
    /** Publishes `v` as this frame's returnValue and returns it. */
    const proto::ProtoObject* finish(const proto::ProtoObject* v);

private:
    proto::ProtoContext frame_;
    proto::ProtoContext* ctx_;
    const proto::ProtoObject** slots_;
    unsigned stackBase_;
    unsigned pendingSlot_;
};

// --- operations, one per opcode group -------------------------------------
// Each forwards to the SAME inline function ExecutionEngine::runLoop calls
// (src/runtime/OpcodeOps.h). There is no second implementation.

const proto::ProtoObject* constant(proto::ProtoContext*, const BlockRec&, std::size_t idx);
const proto::ProtoObject* pushGlobal(proto::ProtoContext*, const BlockRec&, std::size_t idx);
void storeGlobal(proto::ProtoContext*, const BlockRec&, std::size_t idx, const proto::ProtoObject* v);

const proto::ProtoObject* add(proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* sub(proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* mul(proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* lt (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* le (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* gt (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* ge (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* eq (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* ne (proto::ProtoContext*, const proto::ProtoObject* a, const proto::ProtoObject* b);
const proto::ProtoObject* neg(proto::ProtoContext*, const proto::ProtoObject* a);
const proto::ProtoObject* notOp(proto::ProtoContext*, const proto::ProtoObject* a);
/** JUMP_IF_FALSE / JUMP_IF_TRUE: raises when `v` is not a Boolean, as the VM does. */
bool truthy(proto::ProtoContext*, const proto::ProtoObject* v);
const proto::ProtoObject* concat(proto::ProtoContext*, const proto::ProtoObject** vals, unsigned n);

const proto::ProtoObject* makeCell(proto::ProtoContext*);
const proto::ProtoObject* cellGet(proto::ProtoContext*, const proto::ProtoObject* cell);
const proto::ProtoObject* cellSet(proto::ProtoContext*, const proto::ProtoObject* cell,
                                  const proto::ProtoObject* v);
const proto::ProtoObject* makeLazy(proto::ProtoContext*, const proto::ProtoObject* thunk);
const proto::ProtoObject* force(proto::ProtoContext*, const proto::ProtoObject* v);
const proto::ProtoObject* forceThunk(proto::ProtoContext*, const proto::ProtoObject* v);

const proto::ProtoObject* makeFn(proto::ProtoContext*, const BlockRec& enclosing,
                                 std::size_t blockIndex, const proto::ProtoObject** captures,
                                 unsigned captureCount);
const proto::ProtoObject* call(proto::ProtoContext*, const proto::ProtoObject* callee,
                               const proto::ProtoObject** args, unsigned argc);
const proto::ProtoObject* callSpread(proto::ProtoContext*, const proto::ProtoObject* callee,
                                     const proto::ProtoObject** args, unsigned argc,
                                     const proto::ProtoObject* rest);
const proto::ProtoObject* callKw(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                 const proto::ProtoObject** base);
const proto::ProtoObject* send(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                               const proto::ProtoObject** base, unsigned argc);
const proto::ProtoObject* sendApply(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                    const proto::ProtoObject** base, unsigned argc);
const proto::ProtoObject* sendKw(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                 const proto::ProtoObject** base);
const proto::ProtoObject* sendSuper(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                    const proto::ProtoObject** base);

const proto::ProtoObject* makeClass(proto::ProtoContext*, const BlockRec&, std::size_t specIdx,
                                    const proto::ProtoObject** base);
const proto::ProtoObject* construct(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                    const proto::ProtoObject** base);
const proto::ProtoObject* constructSpread(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                          const proto::ProtoObject** base,
                                          const proto::ProtoObject* rest);
const proto::ProtoObject* invokeInit(proto::ProtoContext*, const BlockRec&, std::size_t siteIdx,
                                     const proto::ProtoObject** base);
const proto::ProtoObject* storeField(proto::ProtoContext*, const BlockRec&, std::size_t symIdx,
                                     const proto::ProtoObject* self, const proto::ProtoObject* v);
const proto::ProtoObject* storeFieldIfNew(proto::ProtoContext*, const BlockRec&, std::size_t symIdx,
                                          const proto::ProtoObject* self,
                                          const proto::ProtoObject* v);
void setField(proto::ProtoContext*, const BlockRec&, std::size_t symIdx,
              const proto::ProtoObject* obj, const proto::ProtoObject* v);

bool testType(proto::ProtoContext*, const proto::ProtoObject* v, std::uint32_t typeCode);
bool testProto(proto::ProtoContext*, const BlockRec&, std::size_t symIdx, const proto::ProtoObject* v);
unsigned unapplyFields(proto::ProtoContext*, const BlockRec&, std::size_t namesIdx,
                       const proto::ProtoObject* v, const proto::ProtoObject** out);
void uncons(proto::ProtoContext*, const proto::ProtoObject* list, const proto::ProtoObject** outHead,
            const proto::ProtoObject** outTail);
[[noreturn]] void matchError(proto::ProtoContext*, const proto::ProtoObject* v);
[[noreturn]] void castFail(proto::ProtoContext*, const BlockRec&, std::size_t constIdx,
                           const proto::ProtoObject* v);
const proto::ProtoObject* makeTuple(proto::ProtoContext*, const proto::ProtoObject** elems, unsigned n);

[[noreturn]] void throwValue(proto::ProtoContext*, const proto::ProtoObject* v);
[[noreturn]] void rethrow(proto::ProtoContext*, const proto::ProtoObject* v);
/** The handler entry for `pc`, or nullptr. Same search order as the VM's table. */
const HandlerRec* handlerFor(const BlockRec&, std::size_t pc);
/** A ScalaError as a prelude Throwable instance, for the retry loop's catch. */
const proto::ProtoObject* materialise(proto::ProtoContext*, const char* cls, const char* msg);

/** JUMP_BACK's GC obligation (P4 rule 1). Inlined to one call. */
void safepoint(proto::ProtoContext*);

/** Resolves an import at load time, exactly as the interpreter's D90 path does. */
const proto::ProtoObject* importModule(proto::ProtoContext*, const char* providerSpec,
                                       const char* logicalPath, const char* importerDir);

}  // namespace protoScala::gen
#endif
