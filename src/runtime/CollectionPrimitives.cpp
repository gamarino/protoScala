/*
 * CollectionPrimitives — Vector, Range, Map and Set (DESIGN §6, §6.1).
 *
 * Each is an ordinary protoCore object whose payload is one attribute
 * (plan A0-4): Vector holds a ProtoList under __vec__, Range holds four
 * SmallIntegers, Map and Set hold a ProtoMap under __map__. Map and Set are
 * read and written only through protoCore's hashed-collection helper with the
 * single KeySemantics of scalaKeySemantics() (PROTOMAP-SPEC §4).
 *
 * Every native that re-enters the VM (a map function, a comparator, a
 * KeySemantics callback) opens its own child ProtoContext (P2) and keeps every
 * partial result in a slot of it across the allocation that extends it (P1) —
 * never in a C++ local, which is the Phase 5 Mailbox::push bug.
 */
#include "runtime/PrimitiveSupport.h"
#include "runtime/Primitives.h"

#include "compiler/BytecodeModule.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace protoScala {

namespace {

using namespace prim;
using proto::ProtoContext;
using proto::ProtoList;
using proto::ProtoObject;

// ---------------------------------------------------------------------------
// Range
// ---------------------------------------------------------------------------

struct RangeView {
    long long start = 0, end = 0, step = 1;
    bool inclusive = false;
    // The element count, saturating at 0. O(1): a Range never materialises.
    long long length() const {
        if (step == 0) return 0;
        const long long last = inclusive ? end : (step > 0 ? end - 1 : end + 1);
        if (step > 0 ? last < start : last > start) return 0;
        return (last - start) / step + 1;
    }
    long long at(long long i) const { return start + i * step; }
};

RangeView viewOf(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* r) {
    RangeView v{};
    if (!isRangeFast(ctx, L, r)) throw ScalaError("ClassCastException", "not a Range");
    v.start = proto::asSmallInt(r->getAttribute(ctx, L.rangeStartKey));
    v.end = proto::asSmallInt(r->getAttribute(ctx, L.rangeEndKey));
    v.step = proto::asSmallInt(r->getAttribute(ctx, L.rangeStepKey));
    v.inclusive = r->getAttribute(ctx, L.rangeInclusiveKey) == PROTO_TRUE;
    return v;
}

// D61: a bound must fit a SmallInteger. Boxing both bounds to serve ranges of
// more than 2^53 elements would cost an allocation on every Range method.
long long rangeBound(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (!proto::isSmallInt(v)) {
        if (!isNumberFast(v) && !isCharFast(v)) wrongType(ctx, method, "an Int", v);
        throw ScalaError("IllegalArgumentException",
                         "a Range bound must fit a 54-bit integer");
    }
    return proto::asSmallInt(v);
}

// Immutable instances are rebuilt attribute by attribute (D28), so every
// intermediate object is re-rooted in scope.returnValue before the next
// setAttribute allocates.
const ProtoObject* newRange(ProtoContext* ctx, const RuntimeLayout& L, long long start,
                            long long end, long long step, bool inclusive) {
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* r = L.rangeProto->newChild(&scope, /*isMutable=*/false);
    scope.returnValue = r;
    r = r->setAttribute(&scope, L.rangeStartKey, proto::makeSmallInt(start));
    scope.returnValue = r;
    r = r->setAttribute(&scope, L.rangeEndKey, proto::makeSmallInt(end));
    scope.returnValue = r;
    r = r->setAttribute(&scope, L.rangeStepKey, proto::makeSmallInt(step));
    scope.returnValue = r;
    r = r->setAttribute(&scope, L.rangeInclusiveKey, boolean(inclusive));
    scope.returnValue = r;
    return r;
}

PRIM(prim_int_until) {
    const RuntimeLayout& L = layoutOf();
    const long long a = rangeBound(ctx, self, "until");
    const long long b = rangeBound(ctx, arg(ctx, args, 0, "until", 1), "until");
    return newRange(ctx, L, a, b, 1, /*inclusive=*/false);
}

PRIM(prim_int_to) {
    const RuntimeLayout& L = layoutOf();
    const long long a = rangeBound(ctx, self, "to");
    const long long b = rangeBound(ctx, arg(ctx, args, 0, "to", 1), "to");
    return newRange(ctx, L, a, b, 1, /*inclusive=*/true);
}

PRIM(prim_range_by) {
    const RuntimeLayout& L = layoutOf();
    const RangeView v = viewOf(ctx, L, self);
    const long long s = rangeBound(ctx, arg(ctx, args, 0, "by", 1), "by");
    if (s == 0) throw ScalaError("IllegalArgumentException", "a Range step cannot be zero");
    return newRange(ctx, L, v.start, v.end, s, v.inclusive);
}

PRIM(prim_range_length) {
    expectArgs(ctx, args, "length", 0);
    return proto::makeSmallInt(viewOf(ctx, layoutOf(), self).length());
}

PRIM(prim_range_isEmpty) {
    expectArgs(ctx, args, "isEmpty", 0);
    return boolean(viewOf(ctx, layoutOf(), self).length() == 0);
}

PRIM(prim_range_nonEmpty) {
    expectArgs(ctx, args, "nonEmpty", 0);
    return boolean(viewOf(ctx, layoutOf(), self).length() != 0);
}

PRIM(prim_range_apply) {
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const long long i = intArg(ctx, arg(ctx, args, 0, "apply", 1), "apply");
    if (i < 0 || i >= v.length())
        throw ScalaError("IndexOutOfBoundsException", std::to_string(i));
    return proto::makeSmallInt(v.at(i));
}

PRIM(prim_range_head) {
    expectArgs(ctx, args, "head", 0);
    const RangeView v = viewOf(ctx, layoutOf(), self);
    if (v.length() == 0) throw ScalaError("NoSuchElementException", "head of empty range");
    return proto::makeSmallInt(v.start);
}

PRIM(prim_range_last) {
    expectArgs(ctx, args, "last", 0);
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const long long n = v.length();
    if (n == 0) throw ScalaError("NoSuchElementException", "last of empty range");
    return proto::makeSmallInt(v.at(n - 1));
}

PRIM(prim_range_contains) {
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const ProtoObject* x = arg(ctx, args, 0, "contains", 1);
    if (!proto::isSmallInt(x)) return PROTO_FALSE;
    const long long n = proto::asSmallInt(x);
    const long long len = v.length();
    if (len == 0) return PROTO_FALSE;
    const long long delta = n - v.start;
    if (delta % v.step != 0) return PROTO_FALSE;
    const long long i = delta / v.step;
    return boolean(i >= 0 && i < len);
}

PRIM(prim_range_sum) {
    expectArgs(ctx, args, "sum", 0);
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const long long n = v.length();
    if (n == 0) return proto::makeSmallInt(0);
    // Closed form, so `(0 until 1000000000).sum` does not iterate. The result
    // may exceed a SmallInteger, so it is built through protoCore's arithmetic,
    // which promotes (D1).
    const ProtoObject* first = proto::makeSmallInt(v.start);
    const ProtoObject* last = proto::makeSmallInt(v.at(n - 1));
    const ProtoObject* total = first->add(ctx, last)->multiply(ctx, proto::makeSmallInt(n));
    return total->divide(ctx, proto::makeSmallInt(2));
}

// The list of a Range's elements, built in one context with the accumulator
// re-rooted after every append.
const ProtoList* rangeToList(ProtoContext* ctx, const RangeView& v) {
    const long long n = v.length();
    const ProtoList* out = ctx->newList();
    ctx->returnValue = out->asObject(ctx);
    for (long long i = 0; i < n; ++i) {
        out = out->appendLast(ctx, proto::makeSmallInt(v.at(i)));
        ctx->returnValue = out->asObject(ctx);
    }
    return out;
}

PRIM(prim_range_toList) {
    expectArgs(ctx, args, "toList", 0);
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* r = rangeToList(&scope, viewOf(&scope, layoutOf(), self))->asObject(&scope);
    scope.returnValue = r;
    return r;
}

// scalac answers a Range, not a sequence: `(0 until 5).reverse` is
// `Range 4 to 0 by -1`. Matching it costs four words and no allocation beyond
// the object itself, so it is matched (verified against tools/scala3-3.9.0).
// An empty Range reverses to itself, as scalac's does.
PRIM(prim_range_reverse) {
    expectArgs(ctx, args, "reverse", 0);
    const RuntimeLayout& L = layoutOf();
    const RangeView v = viewOf(ctx, L, self);
    const long long n = v.length();
    if (n == 0) return self;
    return newRange(ctx, L, v.at(n - 1), v.start, -v.step, /*inclusive=*/true);
}

PRIM(prim_range_foreach) {
    const RuntimeLayout& L = layoutOf();
    const RangeView v = viewOf(ctx, L, self);
    const ProtoObject* f = arg(ctx, args, 0, "foreach", 1);
    ExecutionEngine* engine = activeCallContext()->engine;
    const long long n = v.length();
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    for (long long i = 0; i < n; ++i) {
        slot[0] = proto::makeSmallInt(v.at(i));
        (void)engine->invoke(&scope, f, slot, 1);
    }
    return L.unit;
}

// D68: map/flatMap/filter return a List, where Scala returns an IndexedSeq. A
// Range is not closed under them, and IndexedSeq is a Seq trait, which R6 keeps
// out of this phase.
PRIM(prim_range_map) {
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const ProtoObject* f = arg(ctx, args, 0, "map", 1);
    ExecutionEngine* engine = activeCallContext()->engine;
    const long long n = v.length();
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = scope.newList()->asObject(&scope);
    for (long long i = 0; i < n; ++i) {
        slot[1] = proto::makeSmallInt(v.at(i));
        slot[1] = engine->invoke(&scope, f, &slot[1], 1);
        slot[0] = slot[0]->asList(&scope)->appendLast(&scope, slot[1])->asObject(&scope);
    }
    return slot[0];
}

PRIM(prim_range_flatMap) {
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const ProtoObject* f = arg(ctx, args, 0, "flatMap", 1);
    ExecutionEngine* engine = activeCallContext()->engine;
    const long long n = v.length();
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = scope.newList()->asObject(&scope);
    for (long long i = 0; i < n; ++i) {
        slot[1] = proto::makeSmallInt(v.at(i));
        slot[1] = engine->invoke(&scope, f, &slot[1], 1);
        const SeqView inner = seqViewOf(&scope, layoutOf(), slot[1]);
        if (!inner.valid)
            throw ScalaError("IllegalArgumentException",
                             "flatMap expects a collection, got " +
                                 typeName(&scope, layoutOf(), slot[1]));
        for (long long k = 0; k < inner.size; ++k)
            slot[0] = slot[0]->asList(&scope)
                          ->appendLast(&scope, seqElemAt(&scope, inner, k))
                          ->asObject(&scope);
    }
    return slot[0];
}

PRIM(prim_range_filter) {
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const ProtoObject* p = arg(ctx, args, 0, "filter", 1);
    ExecutionEngine* engine = activeCallContext()->engine;
    const long long n = v.length();
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = scope.newList()->asObject(&scope);
    for (long long i = 0; i < n; ++i) {
        slot[1] = proto::makeSmallInt(v.at(i));
        if (engine->invoke(&scope, p, &slot[1], 1) == PROTO_TRUE)
            slot[0] = slot[0]->asList(&scope)->appendLast(&scope, slot[1])->asObject(&scope);
    }
    return slot[0];
}

// `for i <- r if p yield e` goes through withFilter, which the Phase 2
// WithFilter object implements over a List. A Range materialises here, which is
// the one place it does: the filtered result is a List anyway (D68).
PRIM(prim_range_withFilter) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* p = arg(ctx, args, 0, "withFilter", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(3);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = rangeToList(&scope, viewOf(&scope, L, self))->asObject(&scope);
    slot[1] = scope.newList(1, &p)->asObject(&scope);
    const ProtoObject* w = L.withFilterProto->newChild(&scope, /*isMutable=*/false);
    slot[2] = w;
    w = w->setAttribute(&scope, L.listKey, slot[0]);
    slot[2] = w;
    w = w->setAttribute(&scope, L.predsKey, slot[1]);
    slot[2] = w;
    return w;
}

PRIM(prim_range_exists) {
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const ProtoObject* p = arg(ctx, args, 0, "exists", 1);
    ExecutionEngine* engine = activeCallContext()->engine;
    const long long n = v.length();
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    for (long long i = 0; i < n; ++i) {
        slot[0] = proto::makeSmallInt(v.at(i));
        if (engine->invoke(&scope, p, slot, 1) == PROTO_TRUE) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

PRIM(prim_range_forall) {
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const ProtoObject* p = arg(ctx, args, 0, "forall", 1);
    ExecutionEngine* engine = activeCallContext()->engine;
    const long long n = v.length();
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    for (long long i = 0; i < n; ++i) {
        slot[0] = proto::makeSmallInt(v.at(i));
        if (engine->invoke(&scope, p, slot, 1) != PROTO_TRUE) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

PRIM(prim_range_count) {
    const RangeView v = viewOf(ctx, layoutOf(), self);
    const ProtoObject* p = arg(ctx, args, 0, "count", 1);
    ExecutionEngine* engine = activeCallContext()->engine;
    const long long n = v.length();
    long long hits = 0;
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    for (long long i = 0; i < n; ++i) {
        slot[0] = proto::makeSmallInt(v.at(i));
        if (engine->invoke(&scope, p, slot, 1) == PROTO_TRUE) ++hits;
    }
    return proto::makeSmallInt(hits);
}

PRIM(prim_range_find) {
    const RuntimeLayout& L = layoutOf();
    const RangeView v = viewOf(ctx, L, self);
    const ProtoObject* p = arg(ctx, args, 0, "find", 1);
    ExecutionEngine* engine = activeCallContext()->engine;
    const long long n = v.length();
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    for (long long i = 0; i < n; ++i) {
        slot[0] = proto::makeSmallInt(v.at(i));
        if (engine->invoke(&scope, p, slot, 1) == PROTO_TRUE)
            return engine->invoke(&scope, L.hooks.someCompanion, slot, 1);
    }
    return engine->invoke(&scope, L.hooks.noneValue, nullptr, 0);
}

PRIM(prim_range_mkString) {
    const RuntimeLayout& L = layoutOf();
    const RangeView v = viewOf(ctx, L, self);
    const unsigned long argn = argCount(ctx, args);
    std::string start, sep, end;
    if (argn == 1) {
        sep = stringArg(ctx, args->getAt(ctx, 0), "mkString");
    } else if (argn == 3) {
        start = stringArg(ctx, args->getAt(ctx, 0), "mkString");
        sep = stringArg(ctx, args->getAt(ctx, 1), "mkString");
        end = stringArg(ctx, args->getAt(ctx, 2), "mkString");
    } else if (argn != 0) {
        throw ScalaError("IllegalArgumentException",
                         "mkString takes 0, 1 or 3 arguments, got " + std::to_string(argn));
    }
    const long long n = v.length();
    std::string out = start;
    for (long long i = 0; i < n; ++i) {
        if (i) out += sep;
        out += std::to_string(v.at(i));
    }
    return str(ctx, out + end);
}

// scalac's rendering, verified against tools/scala3-3.9.0, including the
// "empty " prefix an empty Range carries.
// Range.toVector: the materialised list wrapped as a Vector. Declared here and
// defined after newVector, which the shared seq section introduces.
const ProtoObject* newVector(ProtoContext* ctx, const RuntimeLayout& L, const ProtoList* data);

PRIM(prim_range_toVector) {
    expectArgs(ctx, args, "toVector", 0);
    const RuntimeLayout& L = layoutOf();
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoList* data = rangeToList(&scope, viewOf(&scope, L, self));
    scope.returnValue = data->asObject(&scope);
    return newVector(&scope, L, data);
}

PRIM(prim_range_toString) {
    expectArgs(ctx, args, "toString", 0);
    const RangeView v = viewOf(ctx, layoutOf(), self);
    std::string s = v.length() == 0 ? "empty Range " : "Range ";
    s += std::to_string(v.start) + (v.inclusive ? " to " : " until ") + std::to_string(v.end);
    if (v.step != 1) s += " by " + std::to_string(v.step);
    return str(ctx, s);
}


// ---------------------------------------------------------------------------
// The shared List / Vector surface
// ---------------------------------------------------------------------------
//
// One implementation per method, installed on BOTH listProto and vectorProto,
// so the two surfaces cannot drift (plan Task 10 Step 2). Each native reads the
// receiver's underlying ProtoList with seqData and rebuilds a result of the
// receiver's own kind with rewrap, so `List.map` answers a List and
// `Vector.map` answers a Vector with no second code path.

// A Vector's payload, or the receiver itself when it is a raw List.
const ProtoList* seqData(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* self,
                         const char* method) {
    if (isListFast(self)) return self->asList(ctx);
    if (isVectorFast(ctx, L, self)) return self->getAttribute(ctx, L.vecDataKey)->asList(ctx);
    wrongType(ctx, method, "a List or a Vector", self);
}

const ProtoObject* newVector(ProtoContext* ctx, const RuntimeLayout& L, const ProtoList* data) {
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* payload = data->asObject(&scope);
    scope.returnValue = payload;
    const ProtoObject* v = L.vectorProto->newChild(&scope, /*isMutable=*/false);
    scope.returnValue = v;
    v = v->setAttribute(&scope, L.vecDataKey, payload);
    scope.returnValue = v;
    return v;
}

// A result of the same kind as the receiver.
const ProtoObject* rewrap(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* self,
                          const ProtoList* data) {
    return isListFast(self) ? data->asObject(ctx) : newVector(ctx, L, data);
}

bool isVectorReceiver(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* self) {
    return !isListFast(self) && isVectorFast(ctx, L, self);
}

// Every element of any Seq kind the argument may be (a List, a Vector or a
// Range), as a plain ProtoList. Used by ++, zip, flatten and the set algebra.
const ProtoList* seqArg(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* v,
                        const char* method) {
    const SeqView sv = seqViewOf(ctx, L, v);
    if (!sv.valid) wrongType(ctx, method, "a collection", v);
    if (!sv.isRange && sv.list) return sv.list;
    const ProtoList* out = ctx->newList();
    ctx->returnValue = out->asObject(ctx);
    for (long long k = 0; k < sv.size; ++k) {
        out = out->appendLast(ctx, seqElemAt(ctx, sv, k));
        ctx->returnValue = out->asObject(ctx);
    }
    return out;
}

long long clampIndex(long long n, long long size) { return n < 0 ? 0 : (n > size ? size : n); }

// --- size and access -------------------------------------------------------

// An alias shares its implementation but keeps its own name, so an
// argument-count error names the method the program actually called.
#define SEQ_NAMED(fn, helper, method) \
    PRIM(fn) { return helper(ctx, self, args, method); }

const ProtoObject* seqLength(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                             const char* method) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, method, 0);
    return ctx->fromInteger(static_cast<long long>(seqData(ctx, L, self, method)->getSize(ctx)));
}
SEQ_NAMED(prim_seq_length, seqLength, "length")
SEQ_NAMED(prim_seq_size, seqLength, "size")

PRIM(prim_seq_isEmpty) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "isEmpty", 0);
    return boolean(seqData(ctx, L, self, "isEmpty")->getSize(ctx) == 0);
}

PRIM(prim_seq_nonEmpty) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "nonEmpty", 0);
    return boolean(seqData(ctx, L, self, "nonEmpty")->getSize(ctx) != 0);
}

PRIM(prim_seq_apply) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "apply");
    const long long i = intArg(ctx, arg(ctx, args, 0, "apply", 1), "apply");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    if (i < 0 || i >= n)
        throw ScalaError("IndexOutOfBoundsException",
                         std::to_string(i) + " is out of bounds (min 0, max " +
                             std::to_string(n - 1) + ")");
    return xs->getAt(ctx, static_cast<int>(i));
}

PRIM(prim_seq_head) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "head", 0);
    const ProtoList* xs = seqData(ctx, L, self, "head");
    if (xs->getSize(ctx) == 0) throw ScalaError("NoSuchElementException", "head of empty list");
    return xs->getAt(ctx, 0);
}

PRIM(prim_seq_last) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "last", 0);
    const ProtoList* xs = seqData(ctx, L, self, "last");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    if (n == 0) throw ScalaError("NoSuchElementException", "last of empty list");
    return xs->getAt(ctx, static_cast<int>(n - 1));
}

const ProtoObject* optionOf(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* v) {
    ExecutionEngine* engine = activeCallContext()->engine;
    proto::ProtoContext scope(ctx->space, ctx);
    if (!v) {
        const ProtoObject* none = engine->invoke(&scope, L.hooks.noneValue, nullptr, 0);
        scope.returnValue = none;
        return none;
    }
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = v;
    const ProtoObject* some = engine->invoke(&scope, L.hooks.someCompanion, slot, 1);
    scope.returnValue = some;
    return some;
}

PRIM(prim_seq_headOption) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "headOption", 0);
    const ProtoList* xs = seqData(ctx, L, self, "headOption");
    return optionOf(ctx, L, xs->getSize(ctx) == 0 ? nullptr : xs->getAt(ctx, 0));
}

PRIM(prim_seq_lastOption) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "lastOption", 0);
    const ProtoList* xs = seqData(ctx, L, self, "lastOption");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    return optionOf(ctx, L, n == 0 ? nullptr : xs->getAt(ctx, static_cast<int>(n - 1)));
}

PRIM(prim_seq_tail) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "tail", 0);
    const ProtoList* xs = seqData(ctx, L, self, "tail");
    if (xs->getSize(ctx) == 0)
        throw ScalaError("UnsupportedOperationException", "tail of empty list");
    return rewrap(ctx, L, self, xs->removeFirst(ctx));
}

PRIM(prim_seq_init) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "init", 0);
    const ProtoList* xs = seqData(ctx, L, self, "init");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    if (n == 0) throw ScalaError("UnsupportedOperationException", "init of empty list");
    return rewrap(ctx, L, self, xs->getSlice(ctx, 0, static_cast<int>(n - 1)));
}

// take/drop clamp rather than throw, as Scala's do.
PRIM(prim_seq_take) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "take");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    const long long k = clampIndex(intArg(ctx, arg(ctx, args, 0, "take", 1), "take"), n);
    return rewrap(ctx, L, self, xs->getSlice(ctx, 0, static_cast<int>(k)));
}

PRIM(prim_seq_drop) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "drop");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    const long long k = clampIndex(intArg(ctx, arg(ctx, args, 0, "drop", 1), "drop"), n);
    return rewrap(ctx, L, self, xs->getSlice(ctx, static_cast<int>(k), static_cast<int>(n)));
}

// The index of the first element the predicate rejects, i.e. takeWhile's length.
long long whilePrefix(ProtoContext* ctx, const ProtoList* xs, const ProtoObject* p,
                      const char* method) {
    const auto n = static_cast<long long>(xs->getSize(ctx));
    for (long long i = 0; i < n; ++i) {
        proto::ProtoContext step(ctx->space, ctx);
        if (!truth(&step, callOne(&step, p, xs->getAt(&step, static_cast<int>(i))), method))
            return i;
    }
    return n;
}

PRIM(prim_seq_takeWhile) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "takeWhile");
    const long long k = whilePrefix(ctx, xs, arg(ctx, args, 0, "takeWhile", 1), "takeWhile");
    return rewrap(ctx, L, self, xs->getSlice(ctx, 0, static_cast<int>(k)));
}

PRIM(prim_seq_dropWhile) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "dropWhile");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    const long long k = whilePrefix(ctx, xs, arg(ctx, args, 0, "dropWhile", 1), "dropWhile");
    return rewrap(ctx, L, self, xs->getSlice(ctx, static_cast<int>(k), static_cast<int>(n)));
}

// A Tuple2 case-class instance, never a ProtoTuple: protoCore interns every
// tuple node and interned tuples are perennial (DESIGN §4.6, R2).
const ProtoObject* makePair(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* a,
                            const ProtoObject* b) {
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = a;
    slot[1] = b;
    // Through the TupleN companion's `apply`, as ActorPrimitives does: the
    // companion has no `<init>` of its own, so `construct` is not the entry
    // point. This is a Tuple2 case-class instance either way, never a
    // ProtoTuple.
    const ProtoObject* r = activeCallContext()->engine->send(&scope, L.tupleCompanion[2],
                                                            L.applyName, slot, 2);
    scope.returnValue = r;
    return r;
}

PRIM(prim_seq_splitAt) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "splitAt");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    const long long k = clampIndex(intArg(ctx, arg(ctx, args, 0, "splitAt", 1), "splitAt"), n);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = rewrap(&scope, L, self, xs->getSlice(&scope, 0, static_cast<int>(k)));
    slot[1] = rewrap(&scope, L, self,
                     xs->getSlice(&scope, static_cast<int>(k), static_cast<int>(n)));
    const ProtoObject* r = makePair(&scope, L, slot[0], slot[1]);
    scope.returnValue = r;
    return r;
}

PRIM(prim_seq_reverse) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "reverse", 0);
    const ProtoList* xs = seqData(ctx, L, self, "reverse");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    ListBuilder b(ctx);
    for (long long i = n; i > 0; --i) b.add(xs->getAt(b.context(), static_cast<int>(i - 1)));
    return rewrap(ctx, L, self, b.finish()->asList(ctx));
}

// --- concatenation and update ----------------------------------------------

PRIM(prim_seq_concat) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "++");
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoList* ys = seqArg(&scope, L, arg(&scope, args, 0, "++", 1), "++");
    const ProtoList* out = xs->extend(&scope, ys);
    scope.returnValue = out->asObject(&scope);
    return rewrap(&scope, L, self, out);
}

PRIM(prim_seq_appended) {   // xs :+ x
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, ":+");
    return rewrap(ctx, L, self, xs->appendLast(ctx, arg(ctx, args, 0, ":+", 1)));
}

PRIM(prim_seq_prepended) {  // x +: xs is xs.+:(x)
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "+:");
    return rewrap(ctx, L, self, xs->appendFirst(ctx, arg(ctx, args, 0, "+:", 1)));
}

PRIM(prim_seq_updated) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "updated");
    const long long i = intArg(ctx, arg(ctx, args, 0, "updated", 2), "updated");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    if (i < 0 || i >= n)
        throw ScalaError("IndexOutOfBoundsException",
                         std::to_string(i) + " is out of bounds (min 0, max " +
                             std::to_string(n - 1) + ")");
    return rewrap(ctx, L, self, xs->setAt(ctx, static_cast<int>(i), args->getAt(ctx, 1)));
}

// --- predicates ------------------------------------------------------------

PRIM(prim_seq_contains) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "contains");
    const ProtoObject* x = arg(ctx, args, 0, "contains", 1);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i)
        if (valuesEqual(ctx, L, xs->getAt(ctx, static_cast<int>(i)), x)) return PROTO_TRUE;
    return PROTO_FALSE;
}

PRIM(prim_seq_indexOf) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "indexOf");
    const ProtoObject* x = arg(ctx, args, 0, "indexOf", 1);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i)
        if (valuesEqual(ctx, L, xs->getAt(ctx, static_cast<int>(i)), x))
            return proto::makeSmallInt(static_cast<long long>(i));
    return proto::makeSmallInt(-1);
}

PRIM(prim_seq_exists) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "exists");
    const ProtoObject* p = arg(ctx, args, 0, "exists", 1);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i) {
        proto::ProtoContext step(ctx->space, ctx);
        if (truth(&step, callOne(&step, p, xs->getAt(&step, static_cast<int>(i))), "exists"))
            return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

PRIM(prim_seq_forall) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "forall");
    const ProtoObject* p = arg(ctx, args, 0, "forall", 1);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i) {
        proto::ProtoContext step(ctx->space, ctx);
        if (!truth(&step, callOne(&step, p, xs->getAt(&step, static_cast<int>(i))), "forall"))
            return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

PRIM(prim_seq_count) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "count");
    const ProtoObject* p = arg(ctx, args, 0, "count", 1);
    long long hits = 0;
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i) {
        proto::ProtoContext step(ctx->space, ctx);
        if (truth(&step, callOne(&step, p, xs->getAt(&step, static_cast<int>(i))), "count")) ++hits;
    }
    return proto::makeSmallInt(hits);
}

PRIM(prim_seq_find) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "find");
    const ProtoObject* p = arg(ctx, args, 0, "find", 1);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i) {
        const ProtoObject* x = xs->getAt(ctx, static_cast<int>(i));
        proto::ProtoContext step(ctx->space, ctx);
        if (truth(&step, callOne(&step, p, x), "find")) return optionOf(ctx, L, x);
    }
    return optionOf(ctx, L, nullptr);
}

PRIM(prim_seq_partition) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "partition");
    const ProtoObject* p = arg(ctx, args, 0, "partition", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = scope.newList()->asObject(&scope);
    slot[1] = scope.newList()->asObject(&scope);
    for (unsigned long i = 0, n = xs->getSize(&scope); i < n; ++i) {
        const ProtoObject* x = xs->getAt(&scope, static_cast<int>(i));
        const unsigned which = truth(&scope, callOne(&scope, p, x), "partition") ? 0u : 1u;
        slot[which] = slot[which]->asList(&scope)->appendLast(&scope, x)->asObject(&scope);
    }
    const ProtoObject* yes = rewrap(&scope, L, self, slot[0]->asList(&scope));
    slot[0] = yes;
    const ProtoObject* no = rewrap(&scope, L, self, slot[1]->asList(&scope));
    slot[1] = no;
    const ProtoObject* r = makePair(&scope, L, slot[0], slot[1]);
    scope.returnValue = r;
    return r;
}

// --- transforms ------------------------------------------------------------

PRIM(prim_seq_map) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "map");
    const ProtoObject* f = arg(ctx, args, 0, "map", 1);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i)
        b.add(callOne(b.context(), f, xs->getAt(b.context(), static_cast<int>(i))));
    return rewrap(ctx, L, self, b.finish()->asList(ctx));
}

PRIM(prim_seq_flatMap) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "flatMap");
    const ProtoObject* f = arg(ctx, args, 0, "flatMap", 1);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i)
        addFlat(b, callOne(b.context(), f, xs->getAt(b.context(), static_cast<int>(i))), "flatMap");
    return rewrap(ctx, L, self, b.finish()->asList(ctx));
}

PRIM(prim_seq_filter) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "filter");
    const ProtoObject* p = arg(ctx, args, 0, "filter", 1);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i) {
        const ProtoObject* x = xs->getAt(b.context(), static_cast<int>(i));
        if (truth(b.context(), callOne(b.context(), p, x), "filter")) b.add(x);
    }
    return rewrap(ctx, L, self, b.finish()->asList(ctx));
}

PRIM(prim_seq_filterNot) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "filterNot");
    const ProtoObject* p = arg(ctx, args, 0, "filterNot", 1);
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i) {
        const ProtoObject* x = xs->getAt(b.context(), static_cast<int>(i));
        if (!truth(b.context(), callOne(b.context(), p, x), "filterNot")) b.add(x);
    }
    return rewrap(ctx, L, self, b.finish()->asList(ctx));
}

// The WithFilter is always over a List: a for-comprehension's result follows the
// generator's `map`, which rewraps, so a Vector generator still yields a Vector.
PRIM(prim_seq_withFilter) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* p = arg(ctx, args, 0, "withFilter", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* preds[1] = {p};
    const ProtoObject* r = makeWithFilter(&scope, seqData(&scope, L, self, "withFilter")->asObject(&scope),
                                          scope.newList(1, preds));
    scope.returnValue = r;
    return r;
}

PRIM(prim_seq_foreach) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "foreach");
    const ProtoObject* f = arg(ctx, args, 0, "foreach", 1);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i)
        (void)callOne(ctx, f, xs->getAt(ctx, static_cast<int>(i)));
    return L.unit;
}

PRIM(prim_seq_zip) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "zip");
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoList* ys = seqArg(&scope, L, arg(&scope, args, 0, "zip", 1), "zip");
    const unsigned long n = std::min(xs->getSize(&scope), ys->getSize(&scope));
    ListBuilder b(&scope);
    for (unsigned long i = 0; i < n; ++i)
        b.add(makePair(b.context(), L, xs->getAt(b.context(), static_cast<int>(i)),
                       ys->getAt(b.context(), static_cast<int>(i))));
    return rewrap(&scope, L, self, b.finish()->asList(&scope));
}

PRIM(prim_seq_zipWithIndex) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "zipWithIndex", 0);
    const ProtoList* xs = seqData(ctx, L, self, "zipWithIndex");
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i)
        b.add(makePair(b.context(), L, xs->getAt(b.context(), static_cast<int>(i)),
                       proto::makeSmallInt(static_cast<long long>(i))));
    return rewrap(ctx, L, self, b.finish()->asList(ctx));
}

PRIM(prim_seq_distinct) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "distinct", 0);
    const ProtoList* xs = seqData(ctx, L, self, "distinct");
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i) {
        const ProtoObject* x = xs->getAt(b.context(), static_cast<int>(i));
        const ProtoList* seen = b.context()->getAutomaticLocal(0)->asList(b.context());
        bool dup = false;
        for (unsigned long k = 0, m = seen->getSize(b.context()); k < m && !dup; ++k)
            dup = valuesEqual(b.context(), L, seen->getAt(b.context(), static_cast<int>(k)), x);
        if (!dup) b.add(x);
    }
    return rewrap(ctx, L, self, b.finish()->asList(ctx));
}

PRIM(prim_seq_flatten) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "flatten", 0);
    const ProtoList* xs = seqData(ctx, L, self, "flatten");
    ListBuilder b(ctx);
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i)
        addFlat(b, xs->getAt(b.context(), static_cast<int>(i)), "flatten");
    return rewrap(ctx, L, self, b.finish()->asList(ctx));
}

// --- conversions -----------------------------------------------------------

const ProtoObject* seqToList(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                             const char* method) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, method, 0);
    return seqData(ctx, L, self, method)->asObject(ctx);
}
SEQ_NAMED(prim_seq_toList, seqToList, "toList")
SEQ_NAMED(prim_seq_toSeq, seqToList, "toSeq")
SEQ_NAMED(prim_seq_iterator, seqToList, "iterator")

PRIM(prim_seq_toVector) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "toVector", 0);
    if (isVectorReceiver(ctx, L, self)) return self;
    return newVector(ctx, L, seqData(ctx, L, self, "toVector"));
}

// --- folds -----------------------------------------------------------------

const ProtoObject* foldImpl(ProtoContext* ctx, const ProtoList* xs, const ProtoObject* z,
                            const ProtoObject* f, bool left) {
    ExecutionEngine* engine = activeCallContext()->engine;
    const auto n = static_cast<long long>(xs->getSize(ctx));
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = z;
    for (long long k = 0; k < n; ++k) {
        const long long i = left ? k : n - 1 - k;
        if (left) {
            slot[1] = xs->getAt(&scope, static_cast<int>(i));
            slot[0] = engine->invoke(&scope, f, slot, 2);   // (acc, x)
        } else {
            // foldRight calls f(x, acc), so the pair is built the other way up.
            const ProtoObject* pair[2] = {xs->getAt(&scope, static_cast<int>(i)), slot[0]};
            slot[1] = pair[0];
            const ProtoObject* argv[2] = {slot[1], slot[0]};
            slot[0] = engine->invoke(&scope, f, argv, 2);
        }
    }
    scope.returnValue = slot[0];
    return slot[0];
}

// A one-shot applier for the curried spelling xs.foldLeft(z)(f), the same
// pattern Actor.spawn(state)(handler) uses (A0-16, C11).
const ProtoObject* makeFoldPartial(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* src,
                                   const ProtoObject* seed, bool left) {
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* o = L.foldPartialProto->newChild(&scope, /*isMutable=*/false);
    scope.returnValue = o;
    o = o->setAttribute(&scope, L.foldSrcKey, src);
    scope.returnValue = o;
    o = o->setAttribute(&scope, L.foldSeedKey, seed);
    scope.returnValue = o;
    o = o->setAttribute(&scope, L.foldLeftKey, boolean(left));
    scope.returnValue = o;
    return o;
}

PRIM(prim_foldPartial_apply) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, "apply", 1);
    const ProtoObject* src = self->getAttribute(ctx, L.foldSrcKey);
    const ProtoObject* seed = self->getAttribute(ctx, L.foldSeedKey);
    const bool left = self->getAttribute(ctx, L.foldLeftKey) == PROTO_TRUE;
    return foldImpl(ctx, seqData(ctx, L, src, "foldLeft"), seed, f, left);
}

// xs.foldLeft(z, f) folds directly; xs.foldLeft(z)(f) gets the one-shot applier.
const ProtoObject* foldEntry(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                             bool left, const char* method) {
    const RuntimeLayout& L = layoutOf();
    const unsigned long n = argCount(ctx, args);
    if (n == 1) return makeFoldPartial(ctx, L, self, args->getAt(ctx, 0), left);
    if (n != 2) wrongArgCount(method, "1 or 2", n);
    return foldImpl(ctx, seqData(ctx, L, self, method), args->getAt(ctx, 0), args->getAt(ctx, 1), left);
}

PRIM(prim_seq_foldLeft) { return foldEntry(ctx, self, args, /*left=*/true, "foldLeft"); }
PRIM(prim_seq_fold) { return foldEntry(ctx, self, args, /*left=*/true, "fold"); }
PRIM(prim_seq_foldRight) { return foldEntry(ctx, self, args, /*left=*/false, "foldRight"); }

const ProtoObject* reduceImpl(ProtoContext* ctx, const ProtoList* xs, const ProtoObject* f,
                              bool left, const char* method) {
    const auto n = static_cast<long long>(xs->getSize(ctx));
    if (n == 0) throw ScalaError("UnsupportedOperationException",
                                 std::string(method) + " of empty collection");
    const int seedIndex = left ? 0 : static_cast<int>(n - 1);
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* seed = xs->getAt(&scope, seedIndex);
    scope.returnValue = seed;
    const ProtoList* rest = left ? xs->getSlice(&scope, 1, static_cast<int>(n))
                                 : xs->getSlice(&scope, 0, static_cast<int>(n - 1));
    scope.returnValue = rest->asObject(&scope);
    const ProtoObject* r = foldImpl(&scope, rest, seed, f, left);
    scope.returnValue = r;
    return r;
}

PRIM(prim_seq_reduce) {
    const RuntimeLayout& L = layoutOf();
    return reduceImpl(ctx, seqData(ctx, L, self, "reduce"), arg(ctx, args, 0, "reduce", 1),
                      /*left=*/true, "reduce");
}
PRIM(prim_seq_reduceLeft) {
    const RuntimeLayout& L = layoutOf();
    return reduceImpl(ctx, seqData(ctx, L, self, "reduceLeft"), arg(ctx, args, 0, "reduceLeft", 1),
                      /*left=*/true, "reduceLeft");
}
PRIM(prim_seq_reduceRight) {
    const RuntimeLayout& L = layoutOf();
    return reduceImpl(ctx, seqData(ctx, L, self, "reduceRight"), arg(ctx, args, 0, "reduceRight", 1),
                      /*left=*/false, "reduceRight");
}

// --- numeric summaries -----------------------------------------------------

PRIM(prim_seq_sum) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "sum", 0);
    const ProtoList* xs = seqData(ctx, L, self, "sum");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* acc = proto::makeSmallInt(0);
    scope.returnValue = acc;
    for (long long i = 0; i < n; ++i) {
        const ProtoObject* x = widenChar(xs->getAt(&scope, static_cast<int>(i)));
        if (!isNumberFast(x)) wrongType(&scope, "sum", "a number", x);
        acc = acc->add(&scope, x);
        scope.returnValue = acc;
    }
    return acc;
}

PRIM(prim_seq_product) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "product", 0);
    const ProtoList* xs = seqData(ctx, L, self, "product");
    const auto n = static_cast<long long>(xs->getSize(ctx));
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* acc = proto::makeSmallInt(1);
    scope.returnValue = acc;
    for (long long i = 0; i < n; ++i) {
        const ProtoObject* x = widenChar(xs->getAt(&scope, static_cast<int>(i)));
        if (!isNumberFast(x)) wrongType(&scope, "product", "a number", x);
        acc = acc->multiply(&scope, x);
        scope.returnValue = acc;
    }
    return acc;
}

// D62: ordering comes from protoCore's `compare`, which orders numbers by exact
// value across SmallInteger/LargeInteger/Double and strings by content. Matching
// Scala would need `Ordering`, i.e. implicits (D3).
//
// The kinds are checked FIRST, and deliberately: protoCore's `compare` happily
// orders two unrelated object cells by their addresses, so relying on it to
// throw would have made `List(new Opaque(1), new Opaque(2)).sorted` answer a
// silently arbitrary order instead of failing. What the runtime can genuinely
// order is numbers (Char included, by code point), strings and booleans.
int compareValues(ProtoContext* ctx, const ProtoObject* a, const ProtoObject* b,
                  const char* method) {
    const ProtoObject* x = widenChar(a);
    const ProtoObject* y = widenChar(b);
    const bool numeric = isNumberFast(x) && isNumberFast(y);
    const bool strings = proto::ProtoObject::isStringTagFast(x) &&
                         proto::ProtoObject::isStringTagFast(y);
    const bool booleans = (x == PROTO_TRUE || x == PROTO_FALSE) &&
                          (y == PROTO_TRUE || y == PROTO_FALSE);
    if (booleans) return x == y ? 0 : (x == PROTO_FALSE ? -1 : 1);   // false < true
    if (!numeric && !strings)
        throw ScalaError("IllegalArgumentException",
                         std::string(method) + " needs comparable elements; use sortWith");
    try {
        return x->compare(ctx, y);
    } catch (const std::exception&) {
        throw ScalaError("IllegalArgumentException",
                         std::string(method) + " needs comparable elements; use sortWith");
    }
}

const ProtoObject* extremum(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* self,
                            const ProtoList* args, const ProtoObject* keyFn, bool wantMax,
                            const char* method) {
    (void)args;
    const ProtoList* xs = seqData(ctx, L, self, method);
    const auto n = static_cast<long long>(xs->getSize(ctx));
    if (n == 0) throw ScalaError("UnsupportedOperationException",
                                 std::string(method) + " of empty collection");
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(4);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = xs->getAt(&scope, 0);                                  // best element
    slot[1] = keyFn ? callOne(&scope, keyFn, slot[0]) : slot[0];      // its key
    for (long long i = 1; i < n; ++i) {
        slot[2] = xs->getAt(&scope, static_cast<int>(i));
        slot[3] = keyFn ? callOne(&scope, keyFn, slot[2]) : slot[2];
        const int c = compareValues(&scope, slot[3], slot[1], method);
        if (wantMax ? c > 0 : c < 0) {
            slot[0] = slot[2];
            slot[1] = slot[3];
        }
    }
    scope.returnValue = slot[0];
    return slot[0];
}

PRIM(prim_seq_min) {
    expectArgs(ctx, args, "min", 0);
    return extremum(ctx, layoutOf(), self, args, nullptr, /*wantMax=*/false, "min");
}
PRIM(prim_seq_max) {
    expectArgs(ctx, args, "max", 0);
    return extremum(ctx, layoutOf(), self, args, nullptr, /*wantMax=*/true, "max");
}
PRIM(prim_seq_minBy) {
    return extremum(ctx, layoutOf(), self, args, arg(ctx, args, 0, "minBy", 1),
                    /*wantMax=*/false, "minBy");
}
PRIM(prim_seq_maxBy) {
    return extremum(ctx, layoutOf(), self, args, arg(ctx, args, 0, "maxBy", 1),
                    /*wantMax=*/true, "maxBy");
}

// --- sorting ---------------------------------------------------------------

// Stable merge sort over a slot array of a dedicated context: the comparator may
// re-enter the VM and allocate, so no element may live in a std::vector across
// it (P1). Indices are plain unsigned and never hold objects.
const ProtoObject* sortSeq(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* self,
                           const ProtoObject* keyFn, const ProtoObject* lessFn,
                           const char* method) {
    const ProtoList* xs = seqData(ctx, L, self, method);
    const auto n = static_cast<unsigned>(xs->getSize(ctx));
    if (n < 2) return self;
    ExecutionEngine* engine = activeCallContext()->engine;
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(3u * n + 2u);
    const ProtoObject** slot = scope.getAutomaticLocals();
    const ProtoObject** elems = slot;            // [0, n)      the elements
    const ProtoObject** keys = slot + n;         // [n, 2n)     keyFn's results, or the elements
    const ProtoObject** scratch = slot + 2u * n; // [2n, 3n)    the result order
    for (unsigned i = 0; i < n; ++i) elems[i] = xs->getAt(&scope, static_cast<int>(i));
    for (unsigned i = 0; i < n; ++i)
        keys[i] = keyFn ? callOne(&scope, keyFn, elems[i]) : elems[i];
    std::vector<unsigned> idx(n), buf(n);        // indices only: no ProtoObject* in std::
    for (unsigned i = 0; i < n; ++i) idx[i] = i;
    auto before = [&](unsigned a, unsigned b) -> bool {
        if (lessFn) {
            slot[3u * n] = elems[a];
            slot[3u * n + 1] = elems[b];
            return truth(&scope, engine->invoke(&scope, lessFn, slot + 3u * n, 2), method);
        }
        return compareValues(&scope, keys[a], keys[b], method) < 0;
    };
    for (unsigned width = 1; width < n; width *= 2) {
        for (unsigned lo = 0; lo < n; lo += 2 * width) {
            const unsigned mid = lo + width < n ? lo + width : n;
            const unsigned hi = lo + 2 * width < n ? lo + 2 * width : n;
            unsigned i = lo, j = mid, k = lo;
            // Strictly-less on the RIGHT element keeps an equal pair in the left
            // run's order, which is what makes the merge stable, as Scala's is.
            while (i < mid && j < hi) buf[k++] = before(idx[j], idx[i]) ? idx[j++] : idx[i++];
            while (i < mid) buf[k++] = idx[i++];
            while (j < hi) buf[k++] = idx[j++];
            for (unsigned t = lo; t < hi; ++t) idx[t] = buf[t];
        }
    }
    for (unsigned i = 0; i < n; ++i) scratch[i] = elems[idx[i]];
    const ProtoList* out = scope.newList(n, scratch);
    scope.returnValue = out->asObject(&scope);
    return rewrap(&scope, L, self, out);
}

PRIM(prim_seq_sorted) {
    expectArgs(ctx, args, "sorted", 0);
    return sortSeq(ctx, layoutOf(), self, nullptr, nullptr, "sorted");
}
PRIM(prim_seq_sortBy) {
    return sortSeq(ctx, layoutOf(), self, arg(ctx, args, 0, "sortBy", 1), nullptr, "sortBy");
}
PRIM(prim_seq_sortWith) {
    return sortSeq(ctx, layoutOf(), self, nullptr, arg(ctx, args, 0, "sortWith", 1), "sortWith");
}

// --- rendering -------------------------------------------------------------

PRIM(prim_seq_mkString) {
    const RuntimeLayout& L = layoutOf();
    const unsigned long argn = argCount(ctx, args);
    std::string start, sep, end;
    if (argn == 1) {
        sep = stringArg(ctx, args->getAt(ctx, 0), "mkString");
    } else if (argn == 3) {
        start = stringArg(ctx, args->getAt(ctx, 0), "mkString");
        sep = stringArg(ctx, args->getAt(ctx, 1), "mkString");
        end = stringArg(ctx, args->getAt(ctx, 2), "mkString");
    } else if (argn != 0) {
        wrongArgCount("mkString", "0, 1 or 3", argn);
    }
    const ProtoList* xs = seqData(ctx, L, self, "mkString");
    std::string out = start;
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i) {
        if (i) out += sep;
        out += show(ctx, L, xs->getAt(ctx, static_cast<int>(i)));
    }
    return str(ctx, out + end);
}

PRIM(prim_vector_toString) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "toString", 0);
    const ProtoList* xs = seqData(ctx, L, self, "toString");
    std::string out = "Vector(";
    for (unsigned long i = 0, n = xs->getSize(ctx); i < n; ++i) {
        if (i) out += ", ";
        out += show(ctx, L, xs->getAt(ctx, static_cast<int>(i)));
    }
    return str(ctx, out + ")");
}

// A Vector's equals and hashCode go through valuesEqual and scalaHash, which
// decide Seq equality over the one SeqView (A0-7), so `a == b` and
// `a.equals(b)` can never disagree.
PRIM(prim_vector_equals) {
    return boolean(valuesEqual(ctx, layoutOf(), self, arg(ctx, args, 0, "equals", 1)));
}

const ProtoObject* vectorHash(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                              const char* method) {
    expectArgs(ctx, args, method, 0);
    return proto::makeSmallInt(scalaHash(ctx, layoutOf(), self));
}
SEQ_NAMED(prim_vector_hashCode, vectorHash, "hashCode")
SEQ_NAMED(prim_vector_hashHash, vectorHash, "##")

PRIM(prim_vectorCompanion_apply) {
    const RuntimeLayout& L = layoutOf();
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoList* data = args ? args : scope.newList();
    scope.returnValue = data->asObject(&scope);
    return newVector(&scope, L, data);
}

PRIM(prim_vectorCompanion_empty) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "empty", 0);
    return newVector(ctx, L, ctx->newList());
}


// ---------------------------------------------------------------------------
// Map and Set (DESIGN §6.1, PROTOMAP-SPEC §4)
// ---------------------------------------------------------------------------
//
// Every read and write goes through protoCore's hashed-collection helper with
// the single KeySemantics below. protoCore's header is explicit that a map used
// with those functions must never be touched with raw setAt/removeAt, because a
// raw SmallInteger key would collide with a hash slot.

// DESIGN §6.1 bullet 1: "objects whose == is eq (instances of classes with the
// default equals, objects, case objects, SINGLETON enum cases, symbols,
// booleans)". Bullet 2: "numbers, chars, strings, case classes (including
// tuples and parameterised enum cases, which are case classes), collections,
// and classes that override equals".
//
// The one judgement call — "does this class override equals?" — is decided by an
// EXACT POINTER COMPARISON against the single method object installed on
// anyProto, pinned in the layout as defaultEqualsMethod. A case class resolves
// to product_equals and a user override to their own compiled method, so both
// fall out of the same comparison with no special case and no heuristic; a
// parameterised enum case needs no rule of its own, which is ruling C2's point.
// Getting this wrong is SILENTLY wrong, which is why the fixtures of
// 18-maps-and-sets are built to fail loudly on a misclassification (plan A0-5).
bool scalaIsIdentityKey(ProtoContext* ctx, const ProtoObject* key) {
    const RuntimeLayout& L = *activeCallContext()->layout;

    // --- bullet 2 first: every value-equality kind, so the fallthrough at the
    //     bottom can default to identity without swallowing one of them.
    if (isNumberFast(key)) return false;                          // numbers
    if (proto::ProtoObject::isStringTagFast(key)) return false;   // strings
    if (isListFast(key)) return false;                            // collections
    if (isVectorFast(ctx, L, key) || isMapFast(ctx, L, key) ||
        isSetFast(ctx, L, key) || isRangeFast(ctx, L, key))
        return false;                                             // collections

    // --- Char is the one special case (maintainer ruling C1, 2026-09-23: "si
    //     Char es un caso especial, debería procesarse especial en el código de
    //     la función. Tratar en lo posible seguir Scala"). A Char is a unique
    //     embedded word, so identity LOOKS right -- but a Char's == is not eq:
    //     Scala's cooperative equality makes 'a' == 97 true and 'a'.## == 97, so
    //     Map('a' -> 1) must be found by a lookup of 97. Sending it down the
    //     value path does that for free, because scalaHash already hashes a Char
    //     to its code point and valuesEqual already widens a Char to it.
    if (isCharFast(key)) return false;

    // --- bullet 1: unique-word immediates. `==` on each of these IS word
    //     identity, which is bullet 1's own criterion.
    if (key == PROTO_TRUE || key == PROTO_FALSE) return true;     // booleans
    if (key == PROTO_NONE || key == L.unit) return true;          // null, ()

    // --- an instance: identity exactly when its equals is the default one.
    if (isScalaInstance(ctx, L, key)) {
        const ProtoObject* eq = key->getAttribute(ctx, L.equalsName);
        return eq == nullptr || eq == PROTO_NONE || eq == L.defaultEqualsMethod;
    }

    // Function values, lazy holders, actors, futures, WithFilter: valuesEqual
    // falls back to identity for all of them, so bullet 1's criterion applies.
    return true;
}

unsigned long scalaKeyHash(ProtoContext* ctx, const ProtoObject* key) {
    // Called only for value-equality keys. scalaHash is Scala's ##: cooperative
    // across Int/Long/Double, Char by code point, String by content, case
    // classes structurally, every Seq kind through the one SeqView.
    return static_cast<unsigned long>(
        static_cast<long long>(scalaHash(ctx, *activeCallContext()->layout, key)));
}

bool scalaKeyEquals(ProtoContext* ctx, const ProtoObject* a, const ProtoObject* b) {
    // Called only inside a collision bucket, on value-equality keys.
    return valuesEqual(ctx, *activeCallContext()->layout, a, b);
}

// A static POD of three function pointers holds no ProtoObject*, so it is not
// the forbidden per-space symbol cache.
const proto::KeySemantics& scalaKeySemantics() {
    static const proto::KeySemantics semantics{&scalaIsIdentityKey, &scalaKeyHash, &scalaKeyEquals};
    return semantics;
}

const proto::ProtoMap* mapDataOf(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* m,
                                 const char* method) {
    if (!isMapFast(ctx, L, m) && !isSetFast(ctx, L, m))
        wrongType(ctx, method, "a Map or a Set", m);
    return m->getAttribute(ctx, L.mapDataKey)->asMap(ctx);
}

const ProtoObject* wrapMap(ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoMap* data,
                           bool isSet) {
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* payload = data->asObject(&scope);
    scope.returnValue = payload;
    const ProtoObject* o = (isSet ? L.setProto : L.mapProto)->newChild(&scope, /*isMutable=*/false);
    scope.returnValue = o;
    o = o->setAttribute(&scope, L.mapDataKey, payload);
    scope.returnValue = o;
    return o;
}

bool selfIsSet(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* self) {
    return isSetFast(ctx, L, self);
}

// The (key, value) of a Tuple2 case-class instance (§4.6), or a loud error.
void pairOf(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* pair, const char* method,
            const ProtoObject** k, const ProtoObject** v) {
    const ProtoObject* a = pair->getAttribute(ctx, L.tupleFieldKey[1]);
    const ProtoObject* b = pair->getAttribute(ctx, L.tupleFieldKey[2]);
    if (!a || a == PROTO_NONE || !b || b == PROTO_NONE) {
        if (!isScalaInstance(ctx, L, pair) ||
            pair->getAttribute(ctx, L.tupleKey) != PROTO_TRUE)
            throw ScalaError("IllegalArgumentException",
                             std::string(method) + " expects a (key, value) pair, got " +
                                 typeName(ctx, L, pair));
    }
    *k = a;
    *v = b;
}

// --- collecting entries ----------------------------------------------------

// Iteration keeps its accumulator in a slot of one context and re-roots it after
// every append: never a std::vector<const ProtoObject*>, which a re-entrant
// hashCode could invalidate (P1).
struct Collector {
    ProtoContext* scope = nullptr;
    const ProtoObject** acc = nullptr;   // one slot, re-rooted after every append
    const RuntimeLayout* L = nullptr;
    int what = 0;                        // 0: pairs, 1: keys, 2: values
};

void collectEntry(ProtoContext* ctx, void* self, const ProtoObject* k, const ProtoObject* v) {
    auto* c = static_cast<Collector*>(self);
    const ProtoObject* item = c->what == 1 ? k : (c->what == 2 ? v : makePair(ctx, *c->L, k, v));
    c->acc[0] = c->acc[0]->asList(ctx)->appendLast(ctx, item)->asObject(ctx);
}

// Every entry of `data` as a List of `what` (pairs, keys or values).
const ProtoObject* entriesOf(ProtoContext* ctx, const RuntimeLayout& L, const proto::ProtoMap* data,
                             int what) {
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = scope.newList()->asObject(&scope);
    Collector c{&scope, slot, &L, what};
    proto::hashedForEach(&scope, data, &c, &collectEntry);
    scope.returnValue = slot[0];
    return slot[0];
}

// --- reads -----------------------------------------------------------------

PRIM(prim_map_apply) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* k = arg(ctx, args, 0, "apply", 1);
    if (selfIsSet(ctx, L, self))   // Set(x) is contains(x) (DESIGN §5.1)
        return boolean(proto::hashedGet(ctx, mapDataOf(ctx, L, self, "apply"),
                                        scalaKeySemantics(), k) != nullptr);
    const ProtoObject* v = proto::hashedGet(ctx, mapDataOf(ctx, L, self, "apply"),
                                            scalaKeySemantics(), k);
    if (!v)   // nullptr, never PROTO_NONE: a stored null stays distinguishable
        throw ScalaError("NoSuchElementException", "key not found: " + show(ctx, L, k));
    return v;
}

PRIM(prim_map_get) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* k = arg(ctx, args, 0, "get", 1);
    const ProtoObject* v = proto::hashedGet(ctx, mapDataOf(ctx, L, self, "get"),
                                            scalaKeySemantics(), k);
    return optionOf(ctx, L, v);       // nullptr -> None, a stored null -> Some(null)
}

PRIM(prim_map_getOrElse) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* k = arg(ctx, args, 0, "getOrElse", 2);
    const ProtoObject* v = proto::hashedGet(ctx, mapDataOf(ctx, L, self, "getOrElse"),
                                            scalaKeySemantics(), k);
    return v ? v : args->getAt(ctx, 1);
}

const ProtoObject* mapContains(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                               const char* method) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* k = arg(ctx, args, 0, method, 1);
    return boolean(proto::hashedGet(ctx, mapDataOf(ctx, L, self, method), scalaKeySemantics(), k) !=
                   nullptr);
}
SEQ_NAMED(prim_map_contains, mapContains, "contains")
SEQ_NAMED(prim_map_isDefinedAt, mapContains, "isDefinedAt")

// The number of ENTRIES, not of slots: a genuine hash collision puts several
// entries in one slot, so ProtoMap::getSize would undercount. Only emptiness can
// be read off the slot count, because a slot always holds at least one entry.
void countEntry(ProtoContext*, void* raw, const ProtoObject*, const ProtoObject*) {
    ++*static_cast<long long*>(raw);
}

long long entryCount(ProtoContext* ctx, const proto::ProtoMap* data) {
    long long n = 0;
    proto::hashedForEach(ctx, data, &n, &countEntry);
    return n;
}

const ProtoObject* mapSize(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                           const char* method) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, method, 0);
    proto::ProtoContext scope(ctx->space, ctx);
    return proto::makeSmallInt(entryCount(&scope, mapDataOf(&scope, L, self, method)));
}
SEQ_NAMED(prim_map_size, mapSize, "size")
SEQ_NAMED(prim_map_length, mapSize, "length")

PRIM(prim_map_isEmpty) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "isEmpty", 0);
    return boolean(mapDataOf(ctx, L, self, "isEmpty")->getSize(ctx) == 0);
}

PRIM(prim_map_nonEmpty) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "nonEmpty", 0);
    return boolean(mapDataOf(ctx, L, self, "nonEmpty")->getSize(ctx) != 0);
}

// D58: the order is ascending hash, deterministic for a given key set but
// unrelated to insertion order or to Scala's, so every fixture sorts.
const ProtoObject* mapEntries(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                              const char* method, int what) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, method, 0);
    const bool set = selfIsSet(ctx, L, self);
    return entriesOf(ctx, L, mapDataOf(ctx, L, self, method), set ? 1 : what);
}

PRIM(prim_map_toList) { return mapEntries(ctx, self, args, "toList", 0); }
PRIM(prim_map_toSeq) { return mapEntries(ctx, self, args, "toSeq", 0); }
PRIM(prim_map_iterator) { return mapEntries(ctx, self, args, "iterator", 0); }
PRIM(prim_map_keys) { return mapEntries(ctx, self, args, "keys", 1); }
PRIM(prim_map_values) { return mapEntries(ctx, self, args, "values", 2); }

PRIM(prim_map_head) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "head", 0);
    const ProtoObject* xs = mapEntries(ctx, self, args, "head", 0);
    const proto::ProtoList* l = xs->asList(ctx);
    if (l->getSize(ctx) == 0)
        throw ScalaError("NoSuchElementException", "head of empty collection");
    (void)L;
    return l->getAt(ctx, 0);
}

// --- writes ----------------------------------------------------------------

const ProtoObject* mapPut(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* self,
                          const ProtoObject* k, const ProtoObject* v, const char* method) {
    proto::ProtoContext scope(ctx->space, ctx);
    scope.returnValue = self;
    const proto::ProtoMap* out = proto::hashedPut(&scope, mapDataOf(&scope, L, self, method),
                                                  scalaKeySemantics(), k, v);
    scope.returnValue = out->asObject(&scope);
    return wrapMap(&scope, L, out, selfIsSet(&scope, L, self));
}

PRIM(prim_map_plus) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* x = arg(ctx, args, 0, "+", 1);
    if (selfIsSet(ctx, L, self)) return mapPut(ctx, L, self, x, x, "+");   // Set: element is entry
    const ProtoObject* k = nullptr;
    const ProtoObject* v = nullptr;
    pairOf(ctx, L, x, "Map.+", &k, &v);
    return mapPut(ctx, L, self, k, v, "+");
}

PRIM(prim_map_updated) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* k = arg(ctx, args, 0, "updated", 2);
    return mapPut(ctx, L, self, k, args->getAt(ctx, 1), "updated");
}

const ProtoObject* mapMinus(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                            const char* method) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* k = arg(ctx, args, 0, method, 1);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.returnValue = self;
    const proto::ProtoMap* out = proto::hashedRemove(&scope, mapDataOf(&scope, L, self, method),
                                                     scalaKeySemantics(), k);
    scope.returnValue = out->asObject(&scope);
    return wrapMap(&scope, L, out, selfIsSet(&scope, L, self));
}
SEQ_NAMED(prim_map_minus, mapMinus, "-")
SEQ_NAMED(prim_map_removed, mapMinus, "removed")

// Every entry of the argument, added to (or removed from) the receiver.
enum class BulkOp { Add, Remove, KeepCommon, KeepMissing };

struct BulkState {
    const RuntimeLayout* L = nullptr;
    const proto::ProtoMap* out = nullptr;
    const proto::ProtoMap* other = nullptr;
    ProtoContext* scope = nullptr;
    const ProtoObject** slot = nullptr;
    BulkOp op = BulkOp::Add;
};

void bulkVisit(ProtoContext* ctx, void* raw, const ProtoObject* k, const ProtoObject* v) {
    auto* s = static_cast<BulkState*>(raw);
    switch (s->op) {
        case BulkOp::Add:
            s->out = proto::hashedPut(ctx, s->out, scalaKeySemantics(), k, v);
            break;
        case BulkOp::Remove:
            s->out = proto::hashedRemove(ctx, s->out, scalaKeySemantics(), k);
            break;
        case BulkOp::KeepCommon:
            if (proto::hashedGet(ctx, s->other, scalaKeySemantics(), k) != nullptr)
                s->out = proto::hashedPut(ctx, s->out, scalaKeySemantics(), k, v);
            break;
        case BulkOp::KeepMissing:
            if (proto::hashedGet(ctx, s->other, scalaKeySemantics(), k) == nullptr)
                s->out = proto::hashedPut(ctx, s->out, scalaKeySemantics(), k, v);
            break;
    }
    s->slot[0] = s->out->asObject(ctx);   // rooted across the next allocation
}

// `other` as a ProtoMap: a Map/Set argument directly, a List/Vector/Range of
// elements (for a Set) or of pairs (for a Map) otherwise.
const proto::ProtoMap* asKeyedData(ProtoContext* ctx, const RuntimeLayout& L,
                                   const ProtoObject* other, bool pairs, const char* method) {
    if (isMapFast(ctx, L, other) || isSetFast(ctx, L, other))
        return other->getAttribute(ctx, L.mapDataKey)->asMap(ctx);
    const SeqView sv = seqViewOf(ctx, L, other);
    if (!sv.valid) wrongType(ctx, method, "a collection", other);
    const proto::ProtoMap* data = ctx->newMap();
    ctx->returnValue = data->asObject(ctx);
    for (long long i = 0; i < sv.size; ++i) {
        const ProtoObject* e = seqElemAt(ctx, sv, i);
        const ProtoObject* k = e;
        const ProtoObject* v = e;
        if (pairs) pairOf(ctx, L, e, method, &k, &v);
        data = proto::hashedPut(ctx, data, scalaKeySemantics(), k, v);
        ctx->returnValue = data->asObject(ctx);
    }
    return data;
}

const ProtoObject* bulk(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                        BulkOp op, const char* method) {
    const RuntimeLayout& L = layoutOf();
    const bool set = selfIsSet(ctx, L, self);
    const ProtoObject* other = arg(ctx, args, 0, method, 1);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    const proto::ProtoMap* mine = mapDataOf(&scope, L, self, method);
    const proto::ProtoMap* theirs = asKeyedData(&scope, L, other, !set, method);
    BulkState st;
    st.L = &L;
    st.scope = &scope;
    st.slot = slot;
    st.op = op;
    if (op == BulkOp::KeepCommon || op == BulkOp::KeepMissing) {
        st.out = scope.newMap();
        st.other = theirs;
        slot[0] = st.out->asObject(&scope);
        proto::hashedForEach(&scope, mine, &st, &bulkVisit);
    } else {
        st.out = mine;
        slot[0] = st.out->asObject(&scope);
        proto::hashedForEach(&scope, theirs, &st, &bulkVisit);
    }
    return wrapMap(&scope, L, st.out, set);
}

PRIM(prim_map_concat) { return bulk(ctx, self, args, BulkOp::Add, "++"); }
PRIM(prim_map_removeAll) { return bulk(ctx, self, args, BulkOp::Remove, "--"); }

const ProtoObject* setUnion(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                            const char* method) {
    return bulk(ctx, self, args, BulkOp::Add, method);
}
SEQ_NAMED(prim_set_union, setUnion, "union")
SEQ_NAMED(prim_set_or, setUnion, "|")

const ProtoObject* setIntersect(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                                const char* method) {
    return bulk(ctx, self, args, BulkOp::KeepCommon, method);
}
SEQ_NAMED(prim_set_intersect, setIntersect, "intersect")
SEQ_NAMED(prim_set_and, setIntersect, "&")

const ProtoObject* setDiff(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                           const char* method) {
    return bulk(ctx, self, args, BulkOp::KeepMissing, method);
}
SEQ_NAMED(prim_set_diff, setDiff, "diff")
SEQ_NAMED(prim_set_andNot, setDiff, "&~")

struct SubsetState {
    const proto::ProtoMap* other = nullptr;
    bool all = true;
};

void subsetVisit(ProtoContext* ctx, void* raw, const ProtoObject* k, const ProtoObject* v) {
    (void)v;
    auto* s = static_cast<SubsetState*>(raw);
    if (s->all && proto::hashedGet(ctx, s->other, scalaKeySemantics(), k) == nullptr) s->all = false;
}

PRIM(prim_set_subsetOf) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* other = arg(ctx, args, 0, "subsetOf", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    SubsetState st{asKeyedData(&scope, L, other, /*pairs=*/false, "subsetOf"), true};
    proto::hashedForEach(&scope, mapDataOf(&scope, L, self, "subsetOf"), &st, &subsetVisit);
    return boolean(st.all);
}

// --- higher-order ----------------------------------------------------------

// A Map's function may be written `(k, v) => ...` (arity 2) or `p => p._1`
// (arity 1). protoScala has no static types, so the arity of the value decides,
// which is what makes both spellings work.
const ProtoObject* callOnEntry(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* f,
                               const ProtoObject* k, const ProtoObject* v, bool isSet) {
    ExecutionEngine* engine = activeCallContext()->engine;
    proto::ProtoContext step(ctx->space, ctx);
    step.resizeAutomaticLocals(2);
    const ProtoObject** slot = step.getAutomaticLocals();
    const BytecodeModule* m = compiledModuleOf(&step, L, f);
    const unsigned arity = m ? (m->isMethod() ? m->arity() - 1 : m->arity()) : 1;
    const ProtoObject* r = nullptr;
    if (isSet || arity == 1) {
        slot[0] = isSet ? k : makePair(&step, L, k, v);
        r = engine->invoke(&step, f, slot, 1);
    } else {
        slot[0] = k;
        slot[1] = v;
        r = engine->invoke(&step, f, slot, 2);
    }
    step.returnValue = r;
    return r;
}

struct EntryFnState {
    const RuntimeLayout* L = nullptr;
    const ProtoObject* f = nullptr;
    bool isSet = false;
    ProtoContext* scope = nullptr;
    const ProtoObject** slot = nullptr;    // slot[0]: the accumulator
    const proto::ProtoMap* out = nullptr;
    long long count = 0;
    bool flag = false;                     // exists/forall/find outcome
    const ProtoObject* found = nullptr;
    bool done = false;
};

void foreachVisit(ProtoContext* ctx, void* raw, const ProtoObject* k, const ProtoObject* v) {
    auto* s = static_cast<EntryFnState*>(raw);
    (void)callOnEntry(ctx, *s->L, s->f, k, v, s->isSet);
}

// map: the result is a Map when the function answered pairs, and a List
// otherwise, which is what Scala's Map.map does with a non-pair result.
void mapVisit(ProtoContext* ctx, void* raw, const ProtoObject* k, const ProtoObject* v) {
    auto* s = static_cast<EntryFnState*>(raw);
    const ProtoObject* r = callOnEntry(ctx, *s->L, s->f, k, v, s->isSet);
    s->slot[1] = r;
    s->slot[0] = s->slot[0]->asList(ctx)->appendLast(ctx, r)->asObject(ctx);
}

void filterVisit(ProtoContext* ctx, void* raw, const ProtoObject* k, const ProtoObject* v) {
    auto* s = static_cast<EntryFnState*>(raw);
    if (!truth(ctx, callOnEntry(ctx, *s->L, s->f, k, v, s->isSet), "filter")) return;
    s->out = proto::hashedPut(ctx, s->out, scalaKeySemantics(), k, v);
    s->slot[0] = s->out->asObject(ctx);
}

void filterNotVisit(ProtoContext* ctx, void* raw, const ProtoObject* k, const ProtoObject* v) {
    auto* s = static_cast<EntryFnState*>(raw);
    if (truth(ctx, callOnEntry(ctx, *s->L, s->f, k, v, s->isSet), "filterNot")) return;
    s->out = proto::hashedPut(ctx, s->out, scalaKeySemantics(), k, v);
    s->slot[0] = s->out->asObject(ctx);
}

void countVisit(ProtoContext* ctx, void* raw, const ProtoObject* k, const ProtoObject* v) {
    auto* s = static_cast<EntryFnState*>(raw);
    if (truth(ctx, callOnEntry(ctx, *s->L, s->f, k, v, s->isSet), "count")) ++s->count;
}

void existsVisit(ProtoContext* ctx, void* raw, const ProtoObject* k, const ProtoObject* v) {
    auto* s = static_cast<EntryFnState*>(raw);
    if (s->done) return;
    if (truth(ctx, callOnEntry(ctx, *s->L, s->f, k, v, s->isSet), "exists")) {
        s->flag = true;
        s->done = true;
        s->found = s->isSet ? k : makePair(ctx, *s->L, k, v);
        s->slot[0] = s->found;
    }
}

void forallVisit(ProtoContext* ctx, void* raw, const ProtoObject* k, const ProtoObject* v) {
    auto* s = static_cast<EntryFnState*>(raw);
    if (s->done) return;
    if (!truth(ctx, callOnEntry(ctx, *s->L, s->f, k, v, s->isSet), "forall")) {
        s->flag = false;
        s->done = true;
    }
}

EntryFnState entryState(const RuntimeLayout& L, ProtoContext* scope, const ProtoObject** slot,
                        const ProtoObject* f, bool isSet) {
    EntryFnState s;
    s.L = &L;
    s.f = f;
    s.isSet = isSet;
    s.scope = scope;
    s.slot = slot;
    return s;
}

PRIM(prim_map_foreach) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, "foreach", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    EntryFnState st = entryState(L, &scope, nullptr, f, selfIsSet(&scope, L, self));
    proto::hashedForEach(&scope, mapDataOf(&scope, L, self, "foreach"), &st, &foreachVisit);
    return L.unit;
}

// Map.map: a pair result rebuilds a Map, anything else gives a List, as Scala's
// does. Set.map always gives a Set.
PRIM(prim_map_map) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, "map", 1);
    const bool set = selfIsSet(ctx, L, self);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = scope.newList()->asObject(&scope);
    EntryFnState st = entryState(L, &scope, slot, f, set);
    proto::hashedForEach(&scope, mapDataOf(&scope, L, self, "map"), &st, &mapVisit);
    const proto::ProtoList* results = slot[0]->asList(&scope);
    const unsigned long n = results->getSize(&scope);
    // A Set rebuilds a Set; a Map rebuilds a Map when every result is a pair.
    bool allPairs = !set;
    for (unsigned long i = 0; i < n && allPairs; ++i) {
        const ProtoObject* e = results->getAt(&scope, static_cast<int>(i));
        allPairs = isScalaInstance(&scope, L, e) &&
                   e->getAttribute(&scope, L.tupleKey) == PROTO_TRUE;
    }
    if (!set && !allPairs) return slot[0];
    const proto::ProtoMap* out = scope.newMap();
    slot[1] = out->asObject(&scope);
    for (unsigned long i = 0; i < n; ++i) {
        const ProtoObject* e = results->getAt(&scope, static_cast<int>(i));
        const ProtoObject* k = e;
        const ProtoObject* v = e;
        if (!set) pairOf(&scope, L, e, "map", &k, &v);
        out = proto::hashedPut(&scope, out, scalaKeySemantics(), k, v);
        slot[1] = out->asObject(&scope);
    }
    return wrapMap(&scope, L, out, set);
}

PRIM(prim_map_flatMap) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, "flatMap", 1);
    const bool set = selfIsSet(ctx, L, self);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(2);
    const ProtoObject** slot = scope.getAutomaticLocals();
    slot[0] = scope.newList()->asObject(&scope);
    EntryFnState st = entryState(L, &scope, slot, f, set);
    proto::hashedForEach(&scope, mapDataOf(&scope, L, self, "flatMap"), &st, &mapVisit);
    // Flatten what the function returned, then rebuild the receiver's kind.
    const proto::ProtoList* results = slot[0]->asList(&scope);
    ListBuilder b(&scope);
    for (unsigned long i = 0, n = results->getSize(&scope); i < n; ++i)
        addFlat(b, results->getAt(b.context(), static_cast<int>(i)), "flatMap");
    slot[0] = b.finish();
    const proto::ProtoList* flat = slot[0]->asList(&scope);
    const unsigned long n = flat->getSize(&scope);
    bool allPairs = !set;
    for (unsigned long i = 0; i < n && allPairs; ++i) {
        const ProtoObject* e = flat->getAt(&scope, static_cast<int>(i));
        allPairs = isScalaInstance(&scope, L, e) &&
                   e->getAttribute(&scope, L.tupleKey) == PROTO_TRUE;
    }
    if (!set && !allPairs) return slot[0];
    const proto::ProtoMap* out = scope.newMap();
    slot[1] = out->asObject(&scope);
    for (unsigned long i = 0; i < n; ++i) {
        const ProtoObject* e = flat->getAt(&scope, static_cast<int>(i));
        const ProtoObject* k = e;
        const ProtoObject* v = e;
        if (!set) pairOf(&scope, L, e, "flatMap", &k, &v);
        out = proto::hashedPut(&scope, out, scalaKeySemantics(), k, v);
        slot[1] = out->asObject(&scope);
    }
    return wrapMap(&scope, L, out, set);
}

const ProtoObject* mapFilter(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                             bool keep, const char* method) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, method, 1);
    const bool set = selfIsSet(ctx, L, self);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    EntryFnState st = entryState(L, &scope, slot, f, set);
    st.out = scope.newMap();
    slot[0] = st.out->asObject(&scope);
    proto::hashedForEach(&scope, mapDataOf(&scope, L, self, method), &st,
                         keep ? &filterVisit : &filterNotVisit);
    return wrapMap(&scope, L, st.out, set);
}

PRIM(prim_map_filter) { return mapFilter(ctx, self, args, /*keep=*/true, "filter"); }
PRIM(prim_map_filterNot) { return mapFilter(ctx, self, args, /*keep=*/false, "filterNot"); }

// A Map's withFilter is over the entry List, so a for-comprehension's guard and
// body interleave exactly as the Phase 2 WithFilter does for a List.
PRIM(prim_map_withFilter) {
    const ProtoObject* p = arg(ctx, args, 0, "withFilter", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* entries = mapEntries(&scope, self, nullptr, "withFilter", 0);
    scope.returnValue = entries;
    const ProtoObject* preds[1] = {p};
    const ProtoObject* r = makeWithFilter(&scope, entries, scope.newList(1, preds));
    scope.returnValue = r;
    return r;
}

PRIM(prim_map_count) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, "count", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    EntryFnState st = entryState(L, &scope, nullptr, f, selfIsSet(&scope, L, self));
    proto::hashedForEach(&scope, mapDataOf(&scope, L, self, "count"), &st, &countVisit);
    return proto::makeSmallInt(st.count);
}

PRIM(prim_map_exists) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, "exists", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    EntryFnState st = entryState(L, &scope, scope.getAutomaticLocals(), f,
                                 selfIsSet(&scope, L, self));
    st.flag = false;
    proto::hashedForEach(&scope, mapDataOf(&scope, L, self, "exists"), &st, &existsVisit);
    return boolean(st.flag);
}

PRIM(prim_map_forall) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, "forall", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    EntryFnState st = entryState(L, &scope, nullptr, f, selfIsSet(&scope, L, self));
    st.flag = true;
    proto::hashedForEach(&scope, mapDataOf(&scope, L, self, "forall"), &st, &forallVisit);
    return boolean(st.flag);
}

PRIM(prim_map_find) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* f = arg(ctx, args, 0, "find", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    EntryFnState st = entryState(L, &scope, scope.getAutomaticLocals(), f,
                                 selfIsSet(&scope, L, self));
    st.flag = false;
    proto::hashedForEach(&scope, mapDataOf(&scope, L, self, "find"), &st, &existsVisit);
    return optionOf(&scope, L, st.flag ? st.found : nullptr);
}

PRIM(prim_map_foldLeft) {
    const RuntimeLayout& L = layoutOf();
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* entries = mapEntries(&scope, self, nullptr, "foldLeft", 0);
    scope.returnValue = entries;
    const unsigned long n = argCount(&scope, args);
    if (n == 1) return makeFoldPartial(&scope, L, entries, args->getAt(&scope, 0), /*left=*/true);
    if (n != 2) wrongArgCount("foldLeft", "1 or 2", n);
    return foldImpl(&scope, entries->asList(&scope), args->getAt(&scope, 0),
                    args->getAt(&scope, 1), /*left=*/true);
}

// --- conversions, rendering, equality --------------------------------------

PRIM(prim_map_toMap) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "toMap", 0);
    if (isMapFast(ctx, L, self)) return self;
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoMap* data = asKeyedData(&scope, L, self, /*pairs=*/true, "toMap");
    scope.returnValue = data->asObject(&scope);
    return wrapMap(&scope, L, data, /*isSet=*/false);
}

const ProtoObject* toSetImpl(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* self) {
    proto::ProtoContext scope(ctx->space, ctx);
    const proto::ProtoMap* data = asKeyedData(&scope, L, self, /*pairs=*/false, "toSet");
    scope.returnValue = data->asObject(&scope);
    return wrapMap(&scope, L, data, /*isSet=*/true);
}

PRIM(prim_seq_toSet) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "toSet", 0);
    if (isSetFast(ctx, L, self)) return self;
    return toSetImpl(ctx, L, self);
}

PRIM(prim_map_keySet) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "keySet", 0);
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* keys = mapEntries(&scope, self, nullptr, "keySet", 1);
    scope.returnValue = keys;
    return toSetImpl(&scope, L, keys);
}

// D58: ascending-hash order, deterministic for a given key set and unrelated to
// Scala's, so the rendering is pinned only by fixtures that sort first.
const ProtoObject* mapShow(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                           const char* method) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, method, 0);
    const bool set = selfIsSet(ctx, L, self);
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* entries = mapEntries(&scope, self, nullptr, method, 0);
    scope.returnValue = entries;
    const proto::ProtoList* l = entries->asList(&scope);
    std::string out = set ? "Set(" : "Map(";
    for (unsigned long i = 0, n = l->getSize(&scope); i < n; ++i) {
        if (i) out += ", ";
        const ProtoObject* e = l->getAt(&scope, static_cast<int>(i));
        if (set) {
            out += show(&scope, L, e);
        } else {
            out += show(&scope, L, e->getAttribute(&scope, L.tupleFieldKey[1])) + " -> " +
                   show(&scope, L, e->getAttribute(&scope, L.tupleFieldKey[2]));
        }
    }
    return str(ctx, out + ")");
}
SEQ_NAMED(prim_map_toString, mapShow, "toString")

PRIM(prim_map_mkString) {
    const RuntimeLayout& L = layoutOf();
    const unsigned long argn = argCount(ctx, args);
    std::string start, sep, end;
    if (argn == 1) {
        sep = stringArg(ctx, args->getAt(ctx, 0), "mkString");
    } else if (argn == 3) {
        start = stringArg(ctx, args->getAt(ctx, 0), "mkString");
        sep = stringArg(ctx, args->getAt(ctx, 1), "mkString");
        end = stringArg(ctx, args->getAt(ctx, 2), "mkString");
    } else if (argn != 0) {
        wrongArgCount("mkString", "0, 1 or 3", argn);
    }
    proto::ProtoContext scope(ctx->space, ctx);
    const ProtoObject* entries = mapEntries(&scope, self, nullptr, "mkString", 0);
    scope.returnValue = entries;
    const proto::ProtoList* l = entries->asList(&scope);
    std::string out = start;
    for (unsigned long i = 0, n = l->getSize(&scope); i < n; ++i) {
        if (i) out += sep;
        out += show(&scope, L, l->getAt(&scope, static_cast<int>(i)));
    }
    return str(ctx, out + end);
}

// Two Maps are equal when they have the same size and every key of one is
// present in the other with an == value; a Map and a Set are never equal.
struct EqState {
    const RuntimeLayout* L = nullptr;
    const proto::ProtoMap* other = nullptr;
    bool same = true;
    bool valuesToo = true;
};

void eqVisit(ProtoContext* ctx, void* raw, const ProtoObject* k, const ProtoObject* v) {
    auto* s = static_cast<EqState*>(raw);
    if (!s->same) return;
    const ProtoObject* w = proto::hashedGet(ctx, s->other, scalaKeySemantics(), k);
    if (!w) { s->same = false; return; }
    if (s->valuesToo && !valuesEqual(ctx, *s->L, v, w)) s->same = false;
}

bool keyedEqual(ProtoContext* ctx, const RuntimeLayout& L, const ProtoObject* a,
                const ProtoObject* b) {
    const bool aSet = isSetFast(ctx, L, a);
    const bool bSet = isSetFast(ctx, L, b);
    if (aSet != bSet) return false;                      // a Map is never a Set
    const proto::ProtoMap* da = a->getAttribute(ctx, L.mapDataKey)->asMap(ctx);
    const proto::ProtoMap* db = b->getAttribute(ctx, L.mapDataKey)->asMap(ctx);
    if (entryCount(ctx, da) != entryCount(ctx, db)) return false;
    EqState st{&L, db, true, !aSet};
    proto::hashedForEach(ctx, da, &st, &eqVisit);
    return st.same;
}

PRIM(prim_map_equals) {
    const RuntimeLayout& L = layoutOf();
    const ProtoObject* other = arg(ctx, args, 0, "equals", 1);
    if (!isMapFast(ctx, L, other) && !isSetFast(ctx, L, other)) return PROTO_FALSE;
    return boolean(keyedEqual(ctx, L, self, other));
}

// Order-independent: a commutative XOR fold, so the ascending-hash iteration
// order cannot leak into the hash (D58).
struct HashState {
    const RuntimeLayout* L = nullptr;
    std::int32_t acc = 0;
    bool keysOnly = false;
};

void hashVisit(ProtoContext* ctx, void* raw, const ProtoObject* k, const ProtoObject* v) {
    auto* s = static_cast<HashState*>(raw);
    const std::int32_t h = s->keysOnly
                               ? scalaHash(ctx, *s->L, k)
                               : scalaHash(ctx, *s->L, k) * 41 + scalaHash(ctx, *s->L, v);
    s->acc ^= h;
}

const ProtoObject* keyedHash(ProtoContext* ctx, const ProtoObject* self, const ProtoList* args,
                             const char* method) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, method, 0);
    proto::ProtoContext scope(ctx->space, ctx);
    HashState st{&L, 0, selfIsSet(&scope, L, self)};
    proto::hashedForEach(&scope, mapDataOf(&scope, L, self, method), &st, &hashVisit);
    return proto::makeSmallInt(st.acc);
}
SEQ_NAMED(prim_map_hashCode, keyedHash, "hashCode")
SEQ_NAMED(prim_map_hashHash, keyedHash, "##")

// --- the companions --------------------------------------------------------

PRIM(prim_mapCompanion_apply) {
    const RuntimeLayout& L = layoutOf();
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    const proto::ProtoMap* data = scope.newMap();
    slot[0] = data->asObject(&scope);
    const unsigned long n = argCount(&scope, args);
    for (unsigned long i = 0; i < n; ++i) {
        const ProtoObject* pair = args->getAt(&scope, static_cast<int>(i));
        const ProtoObject* k = nullptr;
        const ProtoObject* v = nullptr;
        pairOf(&scope, L, pair, "Map(...)", &k, &v);
        data = proto::hashedPut(&scope, data, scalaKeySemantics(), k, v);
        slot[0] = data->asObject(&scope);   // rooted across the next hashedPut
    }
    return wrapMap(&scope, L, data, /*isSet=*/false);
}

PRIM(prim_mapCompanion_empty) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "empty", 0);
    return wrapMap(ctx, L, ctx->newMap(), /*isSet=*/false);
}

PRIM(prim_setCompanion_apply) {
    const RuntimeLayout& L = layoutOf();
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(1);
    const ProtoObject** slot = scope.getAutomaticLocals();
    const proto::ProtoMap* data = scope.newMap();
    slot[0] = data->asObject(&scope);
    const unsigned long n = argCount(&scope, args);
    for (unsigned long i = 0; i < n; ++i) {
        const ProtoObject* e = args->getAt(&scope, static_cast<int>(i));
        data = proto::hashedPut(&scope, data, scalaKeySemantics(), e, e);
        slot[0] = data->asObject(&scope);
    }
    return wrapMap(&scope, L, data, /*isSet=*/true);
}

PRIM(prim_setCompanion_empty) {
    const RuntimeLayout& L = layoutOf();
    expectArgs(ctx, args, "empty", 0);
    return wrapMap(ctx, L, ctx->newMap(), /*isSet=*/true);
}

// --- `->` on Any, and the List operations that build a Map -----------------

// Scala's ArrowAssoc: `k -> v` is the Tuple2 (k, v). A case-class instance,
// never a ProtoTuple (§4.6).
PRIM(prim_any_arrow) {
    const RuntimeLayout& L = layoutOf();
    return makePair(ctx, L, self, arg(ctx, args, 0, "->", 1));
}

// xs.groupBy(f): a Map from each key f produced to the List of elements that
// produced it, in the elements' own order.
PRIM(prim_seq_groupBy) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* xs = seqData(ctx, L, self, "groupBy");
    const ProtoObject* f = arg(ctx, args, 0, "groupBy", 1);
    proto::ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(3);
    const ProtoObject** slot = scope.getAutomaticLocals();
    const proto::ProtoMap* data = scope.newMap();
    slot[0] = data->asObject(&scope);
    for (unsigned long i = 0, n = xs->getSize(&scope); i < n; ++i) {
        slot[1] = xs->getAt(&scope, static_cast<int>(i));
        slot[2] = callOne(&scope, f, slot[1]);
        const ProtoObject* bucket = proto::hashedGet(&scope, data, scalaKeySemantics(), slot[2]);
        const proto::ProtoList* group =
            bucket ? bucket->asList(&scope) : scope.newList();
        const ProtoObject* extended = group->appendLast(&scope, slot[1])->asObject(&scope);
        slot[1] = extended;
        data = proto::hashedPut(&scope, data, scalaKeySemantics(), slot[2], extended);
        slot[0] = data->asObject(&scope);
    }
    return wrapMap(&scope, L, data, /*isSet=*/false);
}

} // namespace

void installCollectionPrimitives(ProtoContext* ctx, const RuntimeLayout& L) {
    using namespace prim;
    static constexpr MethodEntry ranges[] = {
        {"by", &prim_range_by}, {"length", &prim_range_length}, {"size", &prim_range_length},
        {"apply", &prim_range_apply}, {"contains", &prim_range_contains},
        {"toList", &prim_range_toList}, {"toSeq", &prim_range_toList},
        {"toVector", &prim_range_toVector},
        {"foreach", &prim_range_foreach},
        {"map", &prim_range_map}, {"flatMap", &prim_range_flatMap},
        {"filter", &prim_range_filter}, {"withFilter", &prim_range_withFilter},
        {"exists", &prim_range_exists}, {"forall", &prim_range_forall},
        {"count", &prim_range_count}, {"find", &prim_range_find}, {"sum", &prim_range_sum},
        {"isEmpty", &prim_range_isEmpty}, {"nonEmpty", &prim_range_nonEmpty},
        {"head", &prim_range_head}, {"last", &prim_range_last},
        {"reverse", &prim_range_reverse}, {"mkString", &prim_range_mkString},
        {"toString", &prim_range_toString},
    };
    installAll(ctx, L.rangeProto, ranges);
    static constexpr MethodEntry rangeOps[] = {{"until", &prim_int_until}, {"to", &prim_int_to}};
    installAll(ctx, L.intProto, rangeOps);

    // One implementation of each method, installed on both prototypes, so the
    // List and Vector surfaces cannot drift. This runs after installPrimitives'
    // own `lists[]`, and setAttribute overwrites, so where the two overlap these
    // are the ones that answer — and the shadowed entries were removed from
    // `lists[]` in the same change, leaving exactly one implementation.
    static constexpr MethodEntry seqs[] = {
        {"length", &prim_seq_length}, {"size", &prim_seq_size},
        {"isEmpty", &prim_seq_isEmpty}, {"nonEmpty", &prim_seq_nonEmpty},
        {"apply", &prim_seq_apply}, {"head", &prim_seq_head}, {"last", &prim_seq_last},
        {"headOption", &prim_seq_headOption}, {"lastOption", &prim_seq_lastOption},
        {"tail", &prim_seq_tail}, {"init", &prim_seq_init},
        {"take", &prim_seq_take}, {"drop", &prim_seq_drop},
        {"takeWhile", &prim_seq_takeWhile}, {"dropWhile", &prim_seq_dropWhile},
        {"splitAt", &prim_seq_splitAt}, {"reverse", &prim_seq_reverse},
        {"++", &prim_seq_concat}, {":+", &prim_seq_appended}, {"+:", &prim_seq_prepended},
        {"updated", &prim_seq_updated},
        {"contains", &prim_seq_contains}, {"indexOf", &prim_seq_indexOf},
        {"exists", &prim_seq_exists}, {"forall", &prim_seq_forall},
        {"count", &prim_seq_count}, {"find", &prim_seq_find},
        {"partition", &prim_seq_partition},
        {"map", &prim_seq_map}, {"flatMap", &prim_seq_flatMap},
        {"filter", &prim_seq_filter}, {"filterNot", &prim_seq_filterNot},
        {"withFilter", &prim_seq_withFilter}, {"foreach", &prim_seq_foreach},
        {"zip", &prim_seq_zip}, {"zipWithIndex", &prim_seq_zipWithIndex},
        {"distinct", &prim_seq_distinct}, {"flatten", &prim_seq_flatten},
        {"toList", &prim_seq_toList}, {"toSeq", &prim_seq_toSeq},
        {"toVector", &prim_seq_toVector}, {"iterator", &prim_seq_iterator},
        {"foldLeft", &prim_seq_foldLeft}, {"foldRight", &prim_seq_foldRight},
        {"fold", &prim_seq_fold},
        {"reduce", &prim_seq_reduce}, {"reduceLeft", &prim_seq_reduceLeft},
        {"reduceRight", &prim_seq_reduceRight},
        {"sum", &prim_seq_sum}, {"product", &prim_seq_product},
        {"min", &prim_seq_min}, {"max", &prim_seq_max},
        {"minBy", &prim_seq_minBy}, {"maxBy", &prim_seq_maxBy},
        {"sorted", &prim_seq_sorted}, {"sortBy", &prim_seq_sortBy},
        {"sortWith", &prim_seq_sortWith}, {"mkString", &prim_seq_mkString},
    };
    installAll(ctx, L.listProto, seqs);
    installAll(ctx, L.vectorProto, seqs);
    static constexpr MethodEntry vectorOnly[] = {
        {"toString", &prim_vector_toString}, {"equals", &prim_vector_equals},
        {"hashCode", &prim_vector_hashCode}, {"##", &prim_vector_hashHash},
    };
    installAll(ctx, L.vectorProto, vectorOnly);
    static constexpr MethodEntry vectorCompanion[] = {
        {"apply", &prim_vectorCompanion_apply}, {"empty", &prim_vectorCompanion_empty}};
    installAll(ctx, L.vectorCompanion, vectorCompanion);
    static constexpr MethodEntry foldPartial[] = {{"apply", &prim_foldPartial_apply}};
    installAll(ctx, L.foldPartialProto, foldPartial);

    // Map and Set share every native: a Set stores its element as both key and
    // value, so one implementation covers both and `selfIsSet` decides which
    // kind a result is wrapped as.
    static constexpr MethodEntry keyed[] = {
        {"apply", &prim_map_apply}, {"get", &prim_map_get}, {"getOrElse", &prim_map_getOrElse},
        {"contains", &prim_map_contains}, {"isDefinedAt", &prim_map_isDefinedAt},
        {"size", &prim_map_size}, {"length", &prim_map_length},
        {"isEmpty", &prim_map_isEmpty}, {"nonEmpty", &prim_map_nonEmpty},
        {"keys", &prim_map_keys}, {"keySet", &prim_map_keySet}, {"values", &prim_map_values},
        {"toList", &prim_map_toList}, {"toSeq", &prim_map_toSeq},
        {"iterator", &prim_map_iterator}, {"head", &prim_map_head},
        {"+", &prim_map_plus}, {"updated", &prim_map_updated},
        {"-", &prim_map_minus}, {"removed", &prim_map_removed},
        {"++", &prim_map_concat}, {"--", &prim_map_removeAll},
        {"union", &prim_set_union}, {"|", &prim_set_or},
        {"intersect", &prim_set_intersect}, {"&", &prim_set_and},
        {"diff", &prim_set_diff}, {"&~", &prim_set_andNot},
        {"subsetOf", &prim_set_subsetOf},
        {"foreach", &prim_map_foreach}, {"map", &prim_map_map}, {"flatMap", &prim_map_flatMap},
        {"filter", &prim_map_filter}, {"filterNot", &prim_map_filterNot},
        {"withFilter", &prim_map_withFilter},
        {"count", &prim_map_count}, {"exists", &prim_map_exists},
        {"forall", &prim_map_forall}, {"find", &prim_map_find},
        {"foldLeft", &prim_map_foldLeft},
        {"toMap", &prim_map_toMap}, {"toSet", &prim_seq_toSet},
        {"mkString", &prim_map_mkString}, {"toString", &prim_map_toString},
        {"equals", &prim_map_equals}, {"hashCode", &prim_map_hashCode},
        {"##", &prim_map_hashHash},
    };
    installAll(ctx, L.mapProto, keyed);
    installAll(ctx, L.setProto, keyed);
    static constexpr MethodEntry mapCompanion[] = {
        {"apply", &prim_mapCompanion_apply}, {"empty", &prim_mapCompanion_empty}};
    installAll(ctx, L.mapCompanion, mapCompanion);
    static constexpr MethodEntry setCompanion[] = {
        {"apply", &prim_setCompanion_apply}, {"empty", &prim_setCompanion_empty}};
    installAll(ctx, L.setCompanion, setCompanion);
    // `Map(1 -> "a")` needs `->` on every value (Scala's ArrowAssoc).
    static constexpr MethodEntry anyArrow[] = {{"->", &prim_any_arrow}};
    installAll(ctx, L.anyProto, anyArrow);
    // The two Seq operations that build a Map.
    static constexpr MethodEntry seqToMap[] = {
        {"toMap", &prim_map_toMap}, {"toSet", &prim_seq_toSet}, {"groupBy", &prim_seq_groupBy}};
    installAll(ctx, L.listProto, seqToMap);
    installAll(ctx, L.vectorProto, seqToMap);
    static constexpr MethodEntry rangeToSet[] = {{"toSet", &prim_seq_toSet}};
    installAll(ctx, L.rangeProto, rangeToSet);

    L.globals->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Vector"),
                            L.vectorCompanion);
    L.globals->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Map"), L.mapCompanion);
    L.globals->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Set"), L.setCompanion);
}

} // namespace protoScala
