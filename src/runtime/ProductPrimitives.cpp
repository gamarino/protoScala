/*
 * The members Scala synthesises for case classes and tuples (DESIGN §4.5),
 * implemented once as natives on the Product prototype and driven by each
 * class's metadata: __fields__ (the product elements' attribute keys),
 * __prefix__ (productPrefix) and, for tuples, __tuple__. Product sits after
 * every user parent in a case class's chain (Compiler::runtimeChain), so a
 * member the class or a user parent defines wins, as in Scala.
 */
#include "runtime/Hashing.h"
#include "runtime/PrimitiveSupport.h"
#include "runtime/Primitives.h"

#include <string>
#include <vector>

namespace protoScala {

namespace {

using namespace prim;
using proto::ProtoContext;
using proto::ProtoList;
using proto::ProtoObject;
using proto::ProtoString;

// The element keys of `self`'s class; nullptr for a case object.
const ProtoList* fieldsOf(ProtoContext* ctx, const ProtoObject* self) {
    const ProtoObject* f = self->getAttribute(ctx, layoutOf().fieldsKey);
    return isListFast(f) ? f->asList(ctx) : nullptr;
}

const ProtoString* keyAt(ProtoContext* ctx, const ProtoList* fields, unsigned long i) {
    return reinterpret_cast<const ProtoString*>(fields->getAt(ctx, static_cast<int>(i)));
}

const ProtoObject* element(ProtoContext* ctx, const ProtoObject* self, const ProtoList* fields,
                           unsigned long i) {
    const ProtoObject* v = self->getOwnAttributeDirect(ctx, keyAt(ctx, fields, i));
    return v ? v : PROTO_NONE;
}

std::string prefixOf(ProtoContext* ctx, const ProtoObject* self) {
    const ProtoObject* p = self->getAttribute(ctx, layoutOf().prefixKey);
    return ProtoObject::isStringTagFast(p) ? asStr(p)->toStdString(ctx) : std::string();
}

// Point(1,2), (1,a), Unique: elements shown with Scala's toString, no blanks.
PRIM(product_toString) {
    expectArgs(ctx, args, "toString", 0);
    const RuntimeLayout& L = layoutOf();
    const ProtoList* fields = fieldsOf(ctx, self);
    const bool tuple = self->getAttribute(ctx, L.tupleKey) == PROTO_TRUE;
    std::string out = tuple ? "" : prefixOf(ctx, self);
    if (!fields) return str(ctx, out);  // a case object: its name
    out += '(';
    for (unsigned long i = 0, n = fields->getSize(ctx); i < n; ++i) {
        if (i) out += ',';
        out += show(ctx, L, element(ctx, self, fields, i));
    }
    return str(ctx, out + ")");
}

// Structural equality: the same class and == elements (a case object: identity).
PRIM(product_equals) {
    const ProtoObject* other = arg(ctx, args, 0, "equals", 1);
    if (other == self) return PROTO_TRUE;
    if (other == PROTO_NONE || !isObjectCellFast(other) ||
        other->getPrototype(ctx) != self->getPrototype(ctx))
        return PROTO_FALSE;
    const ProtoList* fields = fieldsOf(ctx, self);
    if (!fields) return PROTO_FALSE;
    const RuntimeLayout& L = layoutOf();
    for (unsigned long i = 0, n = fields->getSize(ctx); i < n; ++i)
        if (!valuesEqual(ctx, L, element(ctx, self, fields, i), element(ctx, other, fields, i)))
            return PROTO_FALSE;
    return PROTO_TRUE;
}

// Scala 3's synthesised hashCode (Hashing.h): verified against scalac.
PRIM(product_hashCode) {
    expectArgs(ctx, args, "hashCode", 0);
    const RuntimeLayout& L = layoutOf();
    const ProtoList* fields = fieldsOf(ctx, self);
    std::vector<std::int32_t> hs;
    if (fields)
        for (unsigned long i = 0, n = fields->getSize(ctx); i < n; ++i)
            hs.push_back(scalaHash(ctx, L, element(ctx, self, fields, i)));
    return ctx->fromInteger(hashing::productHash(hashing::javaStringHash(prefixOf(ctx, self)), hs));
}

PRIM(product_productArity) {
    expectArgs(ctx, args, "productArity", 0);
    const ProtoList* fields = fieldsOf(ctx, self);
    return ctx->fromInteger(fields ? static_cast<long long>(fields->getSize(ctx)) : 0);
}

PRIM(product_productPrefix) {
    expectArgs(ctx, args, "productPrefix", 0);
    return str(ctx, prefixOf(ctx, self));
}

const ProtoObject* elementAt(ProtoContext* ctx, const ProtoObject* self, long long i) {
    const ProtoList* fields = fieldsOf(ctx, self);
    const long long n = fields ? static_cast<long long>(fields->getSize(ctx)) : 0;
    if (i < 0 || i >= n)
        throw ScalaError("IndexOutOfBoundsException", "Index out of range: " + std::to_string(i));
    return element(ctx, self, fields, static_cast<unsigned long>(i));
}

PRIM(product_productElement) {
    return elementAt(ctx, self, intArg(ctx, arg(ctx, args, 0, "productElement", 1), "productElement"));
}

// _1 .. _22 (Scala 3 case classes have them; a tuple's own fields shadow these).
#define PRODUCT_ELEMENT(k)                                                 \
    PRIM(product_##k) {                                                    \
        expectArgs(ctx, args, "_" #k, 0);                                  \
        return elementAt(ctx, self, (k) - 1);                              \
    }
PRODUCT_ELEMENT(1)  PRODUCT_ELEMENT(2)  PRODUCT_ELEMENT(3)  PRODUCT_ELEMENT(4)
PRODUCT_ELEMENT(5)  PRODUCT_ELEMENT(6)  PRODUCT_ELEMENT(7)  PRODUCT_ELEMENT(8)
PRODUCT_ELEMENT(9)  PRODUCT_ELEMENT(10) PRODUCT_ELEMENT(11) PRODUCT_ELEMENT(12)
PRODUCT_ELEMENT(13) PRODUCT_ELEMENT(14) PRODUCT_ELEMENT(15) PRODUCT_ELEMENT(16)
PRODUCT_ELEMENT(17) PRODUCT_ELEMENT(18) PRODUCT_ELEMENT(19) PRODUCT_ELEMENT(20)
PRODUCT_ELEMENT(21) PRODUCT_ELEMENT(22)
#undef PRODUCT_ELEMENT

// copy(positional..., name = value...): a new instance through the primary
// constructor (so its body runs, as in Scala); parameters not given keep
// this instance's values. Named arguments arrive in protoCore's keyword
// ProtoSparseList, keyed by the interned name (SEND_KW).
const ProtoObject* product_copy(ProtoContext* ctx, const ProtoObject* self, const proto::ParentLink*,
                                const ProtoList* args, const proto::ProtoSparseList* keywords) {
    const RuntimeLayout& L = layoutOf();
    const ProtoList* fields = fieldsOf(ctx, self);
    // Scala synthesises no copy for a case object, nor for a case class with a
    // repeated parameter (its primary constructor is variadic).
    const ProtoObject* init = self->getPrototype(ctx)->getOwnAttributeDirect(ctx, L.initKey);
    const BytecodeModule* ctor = init ? compiledModuleOf(ctx, L, init) : nullptr;
    if (!fields || (ctor && ctor->isVariadic()))
        throw ScalaError("NoSuchMethodError", "value copy is not a member of " + typeName(ctx, L, self));
    const unsigned long n = fields->getSize(ctx);
    const unsigned long positional = argCount(ctx, args);
    if (positional > n) wrongArgCount("copy", "at most " + std::to_string(n), positional);
    ProtoContext scope(ctx->space, ctx);
    scope.resizeAutomaticLocals(static_cast<unsigned>(n));
    unsigned long named = 0;
    for (unsigned long i = 0; i < n; ++i) {
        const auto key = reinterpret_cast<unsigned long>(keyAt(&scope, fields, i));
        const ProtoObject* v;
        if (i < positional) {
            v = args->getAt(&scope, static_cast<int>(i));
        } else if (keywords && keywords->has(&scope, key)) {
            v = keywords->getAt(&scope, key);
            ++named;
        } else {
            v = element(&scope, self, fields, i);
        }
        scope.setAutomaticLocal(static_cast<unsigned>(i), v);
    }
    if (keywords && named != keywords->getSize(&scope)) {
        // Report a keyword that is not a field; the keyword list's keys are
        // the interned names (ProtoSparseList::processElements, protoCore.h).
        struct Unknown {
            const ProtoList* fields;
            unsigned long n;
            std::string name;
        } unknown{fields, n, {}};
        keywords->processElements(&scope, &unknown,
            [](ProtoContext* c, void* receiver, unsigned long key, const ProtoObject*) {
                auto* u = static_cast<Unknown*>(receiver);
                if (!u->name.empty()) return;
                for (unsigned long i = 0; i < u->n; ++i)
                    if (reinterpret_cast<unsigned long>(keyAt(c, u->fields, i)) == key) return;
                u->name = reinterpret_cast<const ProtoString*>(key)->toStdString(c);
            });
        throw ScalaError("IllegalArgumentException", "copy has no parameter named " + unknown.name);
    }
    const ProtoObject* r = activeCallContext()->engine->construct(
        &scope, self->getPrototype(&scope), scope.getAutomaticLocals(), static_cast<unsigned>(n));
    scope.returnValue = r;
    return r;
}

// TupleN.<init>(this, a1..an): the fields _1.._n (tuples have no Scala body).
PRIM(tuple_init) {
    const RuntimeLayout& L = layoutOf();
    const unsigned long n = argCount(ctx, args);
    const ProtoList* fields = fieldsOf(ctx, self);
    if (!fields || fields->getSize(ctx) != n)
        wrongArgCount("the tuple constructor", std::to_string(fields ? fields->getSize(ctx) : 0), n);
    const ProtoObject* t = self;
    for (unsigned long k = 0; k < n; ++k)
        t = t->setAttribute(ctx, L.tupleFieldKey[k + 1], args->getAt(ctx, static_cast<int>(k)));
    return t;
}

} // namespace

void installProductPrimitives(ProtoContext* ctx, const RuntimeLayout& L) {
    auto put = [&](proto::ProtoObject* target, const char* name, proto::ProtoMethod fn) {
        target->setAttribute(ctx, ProtoString::createSymbol(ctx, name), ctx->fromMethod(nullptr, fn));
    };
    put(L.productProto, "toString", &product_toString);
    put(L.productProto, "equals", &product_equals);
    put(L.productProto, "hashCode", &product_hashCode);
    put(L.productProto, "productArity", &product_productArity);
    put(L.productProto, "productPrefix", &product_productPrefix);
    put(L.productProto, "productElement", &product_productElement);
    put(L.productProto, "copy", &product_copy);
    static constexpr proto::ProtoMethod elements[] = {
        &product_1, &product_2, &product_3, &product_4, &product_5, &product_6, &product_7,
        &product_8, &product_9, &product_10, &product_11, &product_12, &product_13, &product_14,
        &product_15, &product_16, &product_17, &product_18, &product_19, &product_20, &product_21,
        &product_22};
    for (unsigned k = 1; k <= kMaxTupleArity; ++k)
        put(L.productProto, ("_" + std::to_string(k)).c_str(), elements[k - 1]);
    for (unsigned n = 2; n <= kMaxTupleArity; ++n) put(L.tupleProto[n], "<init>", &tuple_init);
}

} // namespace protoScala
