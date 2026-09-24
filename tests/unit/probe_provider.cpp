/*
 * A TEST DOUBLE, not a runtime. It registers a protoCore ModuleProvider under
 * the family alias `js` so the `js.` prefix has something to route to, and it
 * answers exactly two logical paths with hand-built objects. It is built only by
 * the test target and is never installed.
 *
 * It exists to prove the ROUTING, the BOUNDARY SHAPE and the KEYWORD
 * CONVENTION, and its fixtures are named for the stand-in rather than for a real
 * library. A test double answering as if it were numpy would be a green test
 * asserting numpy behaviour against a stub, which is the failure mode this family
 * already learned to reject (Phase 6 plan A0-12). It answers `probe`, never a
 * real library's data.
 *
 * It registers under `js` because it needs one of the four family aliases and
 * protoJS registers none, so nothing is shadowed.
 *
 * This translation unit links protoCore only: no protoscala_runtime, so `show`
 * and the layout are out of reach and `describe` below is written locally.
 */
#include "protoCore.h"
#include "umd/ProviderPlugins.h"

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

// A SmallInteger or a ProtoString as text, anything else as `?`. Three lines,
// written here rather than reached for in the runtime.
std::string describe(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE) return "null";
    if (v == PROTO_TRUE) return "true";
    if (v == PROTO_FALSE) return "false";
    if (const proto::ProtoString* s = v->asString(ctx)) return s->toStdString(ctx);
    // asIntegerString throws when the receiver is not an integer, which for a
    // test double is the right amount of effort.
    try {
        if (const proto::ProtoString* n = v->asIntegerString(ctx)) return n->toStdString(ctx);
    } catch (const std::exception&) {
    }
    return "?";
}

// One keyword argument, read the way INTEROP §7 says a foreign callee must: the
// key is the ADDRESS of the interned parameter-name symbol, so it is looked up
// with the address `createSymbol` returns and nothing else. `fromUTF8String`
// would work by accident for a short name (protoCore embeds a short string in
// the pointer word) and fail silently for one too long to embed, which is
// exactly the trap the convention documents.
const proto::ProtoObject* keywordNamed(proto::ProtoContext* ctx,
                                       const proto::ProtoSparseList* keywords, const char* name) {
    if (!keywords) return nullptr;
    const auto* sym = proto::ProtoString::createSymbol(ctx, name);
    const auto key = reinterpret_cast<std::uintptr_t>(sym);
    if (!keywords->has(ctx, key)) return nullptr;
    const proto::ProtoObject* v = keywords->getAt(ctx, key);
    return (v && v != PROTO_NONE) ? v : nullptr;
}

// __probe.echo(a, b = <value>) -> "echo a=<a> b=<b>", so a fixture can show that
// a named argument crossed a REAL UMD boundary and arrived in the callee's
// keywordParameters. `aVeryLongKeywordName` is accepted under the same name so a
// second fixture can use a parameter name too long to embed in a pointer word.
const proto::ProtoObject* probe_echo(proto::ProtoContext* ctx, const proto::ProtoObject*,
                                     const proto::ParentLink*, const proto::ProtoList* positional,
                                     const proto::ProtoSparseList* keywords) {
    std::string out = "echo";
    if (positional && positional->getSize(ctx) > 0)
        out += " a=" + describe(ctx, positional->getAt(ctx, 0));
    if (const proto::ProtoObject* v = keywordNamed(ctx, keywords, "b"))
        out += " b=" + describe(ctx, v);
    else if (const proto::ProtoObject* w = keywordNamed(ctx, keywords, "aVeryLongKeywordName"))
        out += " b=" + describe(ctx, w);
    return ctx->fromUTF8String(out.c_str());
}

// A native that throws a C++ exception, so a fixture can show that it arrives at
// the Scala call site as a catchable RuntimeException with its message intact.
const proto::ProtoObject* probe_boom(proto::ProtoContext*, const proto::ProtoObject*,
                                     const proto::ParentLink*, const proto::ProtoList*,
                                     const proto::ProtoSparseList*) {
    throw std::runtime_error("provider exploded");
}

// A native that throws something that is NOT a std::exception. Without the last
// clause of the boundary shape this escapes the VM and terminates the process.
const proto::ProtoObject* probe_throwInt(proto::ProtoContext*, const proto::ProtoObject*,
                                        const proto::ParentLink*, const proto::ProtoList*,
                                        const proto::ProtoSparseList*) {
    throw 42;
}

class ProbeProvider : public proto::ModuleProvider {
public:
    ProbeProvider() : guid_("protoScala-probe-v1"), alias_("js") {}

    const proto::ProtoObject* tryLoad(const std::string& logicalPath,
                                      proto::ProtoContext* ctx) override {
        // Anything else is a MISS, not an error: protoCore's resolver treats
        // PROTO_NONE as "try the next chain entry".
        if (logicalPath != "probe") return PROTO_NONE;
        // Mutable: an immutable object's setAttribute answers a NEW object, so
        // the exports written after the first one would land elsewhere.
        auto* mod = const_cast<proto::ProtoObject*>(
            ctx->space->objectPrototype->newChild(ctx, /*isMutable=*/true));
        mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "name"),
                          ctx->fromUTF8String("probe"));
        mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "echo"),
                          ctx->fromMethod(mod, &probe_echo));
        mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "boom"),
                          ctx->fromMethod(mod, &probe_boom));
        mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "boomInt"),
                          ctx->fromMethod(mod, &probe_throwInt));
        // A member that legitimately holds null, so a test can show that
        // bindForeignMember tells "absent" from "null" with hasAttribute rather
        // than by comparing the value with PROTO_NONE.
        mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "nothing"), PROTO_NONE);
        return mod;
    }

    const std::string& getGUID() const override { return guid_; }
    const std::string& getAlias() const override { return alias_; }

private:
    std::string guid_, alias_;
};

}  // namespace

extern "C" const char* protoScalaProviderABI() { return protoScala::kProviderPluginABI; }

extern "C" int protoScalaRegisterProviders(proto::ProtoSpace*) {
    proto::ProviderRegistry::instance().registerProvider(std::make_unique<ProbeProvider>());
    return 0;
}
