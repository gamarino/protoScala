/*
 * The FOREIGN half of the cross-runtime call (Phase 7 §D3, INTEROP §8).
 *
 * This translation unit stands in for another runtime built on the same object kernel.
 * It includes protoCore.h and ForeignHost.h, and nothing else: it names no type, header
 * or symbol of protoScala, has no access to its ExecutionEngine, and knows nothing
 * about its bytecode. `foreign-call.sh` greps this file and fails if the string
 * "protoScala" appears in it outside this comment, so the property is checked rather
 * than asserted.
 *
 * What it does is what any protoCore-based runtime does with any object: read an
 * attribute, see that it is a method, and call it.
 */
#include <protoCore.h>

#include "ForeignHost.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int failures = 0;

// Another runtime's exception: protoCore's one exception type, with something of its
// own on top (a traceback, say). protoCore rule E3: a handler that does not select it
// lets it leave as the same C++ object, so the extra survives.
struct TracedError : proto::ProtoException {
    TracedError(proto::ProtoContext* ctx, const proto::ProtoObject* v)
        : proto::ProtoException(ctx, v) {}
    int traceDepth = 17;
};

const proto::ProtoObject* g_tracedValue = nullptr;

const proto::ProtoObject* raiseTraced(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                      const proto::ParentLink*, const proto::ProtoArguments*,
                                      const proto::ProtoKeywordArguments*) {
    throw TracedError(ctx, g_tracedValue);
}

void fail(const std::string& what) {
    std::fprintf(stderr, "FAIL: %s\n", what.c_str());
    ++failures;
}

/** The one operation under test: call an exported function with protoCore alone. */
const proto::ProtoObject* callExport(proto::ProtoContext* ctx, const proto::ProtoObject* module,
                                     const char* name, const proto::ProtoList* args) {
    const proto::ProtoString* key = proto::ProtoString::createSymbol(ctx, name);
    const proto::ProtoObject* fn = module->getAttribute(ctx, key);
    if (!fn || fn == PROTO_NONE) {
        fail(std::string("the module exports no '") + name + "'");
        return nullptr;
    }
    if (!fn->isMethod(ctx)) {
        fail(std::string("'") + name + "' is not a method cell");
        return nullptr;
    }
    const proto::ProtoMethod m = fn->asMethod(ctx);
    if (!m) {
        fail(std::string("'") + name + "' has no method pointer");
        return nullptr;
    }
    const proto::ProtoListArguments positional(ctx, args);
    return m(ctx, fn->asMethodSelf(ctx), nullptr, &positional, nullptr);
}

long long asInt(proto::ProtoContext* ctx, const proto::ProtoObject* v, const char* what) {
    if (!v || !proto::isSmallInt(v)) {
        fail(std::string(what) + " did not return a small integer");
        return -1;
    }
    (void)ctx;
    return proto::asSmallInt(v);
}

int body(proto::ProtoContext* ctx, const proto::ProtoObject* module, void* ud) {
    (void)ud;
    if (!module || module == PROTO_NONE) {
        fail("the module object is absent");
        return 1;
    }

    // add(3, 4) == 7: two arguments, arithmetic, a returned Int.
    {
        const proto::ProtoList* args =
            ctx->newList()->appendLast(ctx, ctx->fromInteger(3))->appendLast(ctx,
                                                                            ctx->fromInteger(4));
        const proto::ProtoObject* r = callExport(ctx, module, "add", args);
        if (r && asInt(ctx, r, "add") != 7) fail("add(3, 4) != 7");
    }

    // A function whose body needs the prelude and a local: sumTo(10) == 55. This one
    // would be the first to break if the call ran without the host's layout.
    {
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromInteger(10));
        const proto::ProtoObject* r = callExport(ctx, module, "sumTo", args);
        if (r && asInt(ctx, r, "sumTo") != 55) fail("sumTo(10) != 55");
    }

    // A String result, read back as bytes: greet("world") == "hello, world".
    {
        const proto::ProtoList* args =
            ctx->newList()->appendLast(
                ctx, proto::ProtoString::fromUTF8(ctx, "world")->asObject(ctx));
        const proto::ProtoObject* r = callExport(ctx, module, "greet", args);
        if (r) {
            if (!r->isString(ctx)) {
                fail("greet did not return a string");
            } else {
                std::string s;
                r->asString(ctx)->toUTF8String(ctx, s);
                if (s != "hello, world") fail("greet(\"world\") == '" + s + "'");
            }
        }
    }

    // A function that raises: the exception must cross the boundary as protoCore's
    // one exception type, proto::ProtoException carrying the language's exception
    // object (protoCore 3.0.0, docs/EXCEPTIONS.md), not as a std:: exception and not
    // by aborting the process.
    {
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, ctx->fromInteger(0));
        bool threw = false;
        try {
            (void)callExport(ctx, module, "reciprocal", args);
        } catch (const proto::ProtoException& e) {
            threw = true;
            if (!e.value() || e.value() == PROTO_NONE)
                fail("reciprocal(0) raised a ProtoException carrying no value");
        } catch (const std::exception& e) {
            threw = true;
            fail(std::string("reciprocal(0) left the module as a std:: exception: ") + e.what());
        }
        if (!threw) fail("reciprocal(0) did not raise");
    }

    // Rule E3, through a non-matching catch and through a finally: the caller's own
    // exception subclass comes back as the same C++ type, with its extra field and the
    // very value it was raised with.
    for (const char* through : {"passThroughCatch", "passThroughFinally"}) {
        g_tracedValue = ctx->newObject(false);
        const proto::ProtoObject* raiser = ctx->fromMethod(nullptr, &raiseTraced);
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, raiser);
        bool arrived = false;
        try {
            (void)callExport(ctx, module, through, args);
        } catch (const proto::ProtoException& e) {
            arrived = true;
            const auto* traced = dynamic_cast<const TracedError*>(&e);
            if (!traced)
                fail(std::string(through) + ": the exception left as a different C++ object");
            else if (traced->traceDepth != 17)
                fail(std::string(through) + ": the subclass's own field was lost");
            if (e.value() != g_tracedValue)
                fail(std::string(through) + ": the exception left with a different value");
        } catch (...) {
            arrived = true;
            fail(std::string(through) + ": the exception left as something else");
        }
        if (!arrived) fail(std::string(through) + ": nothing was raised");
        g_tracedValue = nullptr;
    }

    // A function that is absent must read as absent, not as a wrong cell.
    {
        const proto::ProtoString* key = proto::ProtoString::createSymbol(ctx, "notThere");
        const proto::ProtoObject* v = module->getAttribute(ctx, key);
        if (v && v != PROTO_NONE) fail("a name the module does not export resolved to something");
    }

    if (failures == 0) std::printf("OK\n");
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: foreign_caller <module.so>\n");
        return 2;
    }
    return foreignHostWithModule(argv[1], &body, nullptr);
}
