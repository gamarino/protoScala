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

#include <algorithm>
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
    L.globals->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "Vector"),
                            L.vectorCompanion);
}

} // namespace protoScala
