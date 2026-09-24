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

#include <string>

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
PRIM(prim_range_toString) {
    expectArgs(ctx, args, "toString", 0);
    const RangeView v = viewOf(ctx, layoutOf(), self);
    std::string s = v.length() == 0 ? "empty Range " : "Range ";
    s += std::to_string(v.start) + (v.inclusive ? " to " : " until ") + std::to_string(v.end);
    if (v.step != 1) s += " by " + std::to_string(v.step);
    return str(ctx, s);
}

} // namespace

void installCollectionPrimitives(ProtoContext* ctx, const RuntimeLayout& L) {
    using namespace prim;
    static constexpr MethodEntry ranges[] = {
        {"by", &prim_range_by}, {"length", &prim_range_length}, {"size", &prim_range_length},
        {"apply", &prim_range_apply}, {"contains", &prim_range_contains},
        {"toList", &prim_range_toList}, {"toSeq", &prim_range_toList},
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
}

} // namespace protoScala
