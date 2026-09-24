/*
 * buildPreludeImage — the reconstruction half of the precompiled prelude.
 *
 * This file is HAND-WRITTEN and reviewed; only the tables it walks
 * (generated/PreludeImage.cpp) change when lib/prelude.scala changes. That is
 * what makes the reconstruction unable to drift per prelude.
 *
 * Two invariants are silent if broken, and tests/unit/test_prelude_image.cpp
 * pins both by comparing disassemble() output with the source path:
 *
 *  - CONSTANT-POOL INDICES must come out identical. The pool de-duplicates by
 *    kind, so the reconstruction REPLAYS addInt/addString/addSendSite/... in the
 *    compiler's original order rather than assigning slots; assigning slots
 *    directly would run and be wrong.
 *  - BLOCK INDICES must come out identical, because MAKE_FN's operand is a block
 *    index baked into the code words.
 */
#include "support/PreludeImage.h"

#if defined(PROTOSCALA_HAVE_PRELUDE_IMAGE)

#include <stdexcept>
#include <string>
#include <vector>

namespace protoScala {
namespace {

const PreludeImageData& D() { return preludeImageData(); }

std::vector<std::string> stringsAt(int first, int count) {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int k = 0; k < count; ++k)
        out.emplace_back(D().strings[static_cast<std::size_t>(first + k)]);
    return out;
}

std::vector<std::uint32_t> masksAt(int first, int count) {
    std::vector<std::uint32_t> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int k = 0; k < count; ++k) out.push_back(D().masks[static_cast<std::size_t>(first + k)]);
    return out;
}

void replayConst(BytecodeModule& m, const PreludeConstRec& c) {
    using K = BytecodeModule::ConstKind;
    switch (static_cast<K>(c.kind)) {
        case K::Int:      m.addInt(c.ival); return;
        case K::BigInt:   m.addBigInt(c.sval, c.base); return;
        case K::Double:   m.addDouble(c.dval); return;
        case K::String:   m.addString(c.sval); return;
        case K::Char:     m.addChar(static_cast<char32_t>(c.ival)); return;
        case K::Symbol:   m.addSymbol(c.sval); return;
        case K::SendSite: m.addSendSite(c.sval, c.argc, c.key); return;
        case K::Names:    m.addNames(stringsAt(c.namesFirst, c.namesCount)); return;
        case K::ClassSpec: {
            BytecodeModule::ClassSpecData spec;
            spec.displayName = c.sval;
            spec.key = c.key;
            spec.parentCount = c.argc;
            spec.memberKeys = stringsAt(c.namesFirst, c.namesCount);
            spec.fields = stringsAt(c.fieldsFirst, c.fieldsCount);
            spec.flags = c.flags;
            m.addClassSpec(spec);
            return;
        }
        case K::SuperSite:  m.addSuperSite(c.sval, c.argc, c.key, c.exact); return;
        case K::KwSendSite:
            m.addKwSendSite(c.sval, c.argc, stringsAt(c.namesFirst, c.namesCount), c.key);
            return;
    }
    throw std::logic_error("prelude image: unknown constant kind " + std::to_string(c.kind));
}

ClassInfo classInfoOf(const PreludeTypeRec& t) {
    ClassInfo c;
    c.name = t.name;
    c.key = t.key;
    c.kind = static_cast<ClassKind>(t.kind);
    c.isCase = (t.flags & kPTCase) != 0;
    c.isAbstract = (t.flags & kPTAbstract) != 0;
    c.isFinal = (t.flags & kPTFinal) != 0;
    c.isSealed = (t.flags & kPTSealed) != 0;
    c.builtin = (t.flags & kPTBuiltin) != 0;
    c.mutableInstances = (t.flags & kPTMutableInstances) != 0;
    c.hasInit = (t.flags & kPTHasInit) != 0;
    c.primaryVariadic = (t.flags & kPTPrimaryVariadic) != 0;
    c.companionHasApply = (t.flags & kPTCompanionHasApply) != 0;
    c.companionHasUnapply = (t.flags & kPTCompanionHasUnapply) != 0;
    c.linearization = stringsAt(t.linFirst, t.linCount);
    c.fields = stringsAt(t.fieldFirst, t.fieldCount);
    c.ctorParams = stringsAt(t.ctorFirst, t.ctorCount);
    c.primaryArity = t.primaryArity;
    c.primaryMinArity = t.primaryMinArity;
    c.primaryByNameMasks = masksAt(t.primaryMaskFirst, t.primaryMaskCount);
    for (int k = 0; k < t.auxCount; ++k)
        c.auxArities.push_back(D().auxArities[static_cast<std::size_t>(t.auxFirst + k)]);
    for (int k = 0; k < t.memberCount; ++k) {
        const PreludeMemberRec& r = D().members[static_cast<std::size_t>(t.memberFirst + k)];
        MemberInfo mi;
        mi.kind = static_cast<MemberKind>(r.kind);
        mi.key = r.key;
        mi.concrete = r.concrete;
        mi.byNameValue = r.byNameValue;
        mi.byNameMasks = masksAt(r.maskFirst, r.maskCount);
        c.members.emplace(r.name, std::move(mi));
    }
    c.companionTermKey = t.companionTermKey;
    c.companionTypeKey = t.companionTypeKey;
    return c;
}

}  // namespace

const BytecodeModule& buildPreludeImage(GlobalTable& globals,
                                        std::vector<std::unique_ptr<BytecodeModule>>& modules) {
    const PreludeImageData& d = D();
    if (d.moduleCount == 0) throw std::logic_error("prelude image: no module in the image");

    std::vector<std::unique_ptr<BytecodeModule>> built(d.moduleCount);
    for (std::size_t i = 0; i < d.moduleCount; ++i) {
        const PreludeModuleRec& r = d.modules[i];
        auto m = std::make_unique<BytecodeModule>();
        m->setName(r.name);
        m->setArity(r.arity);
        m->setVariadic(r.variadic);
        m->setLocalCount(r.localCount);
        m->setMaxStack(r.maxStack);
        m->setMethod(r.method);
        m->setParamless(r.paramless);
        for (int k = 0; k < r.constCount; ++k)
            replayConst(*m, d.consts[static_cast<std::size_t>(r.constFirst + k)]);
        for (int k = 0; k < r.codeCount; ++k) {
            const std::size_t at = static_cast<std::size_t>(r.codeFirst + k);
            m->appendRawInstr(d.code[at], d.lines[at]);
        }
        for (int k = 0; k < r.handlerCount; ++k) {
            const PreludeHandlerRec& h = d.handlers[static_cast<std::size_t>(r.handlerFirst + k)];
            m->addHandler(BytecodeModule::Handler{h.startPc, h.endPc, h.handlerPc, h.stackDepth,
                                                  h.slot,
                                                  static_cast<BytecodeModule::HandlerKind>(h.kind)});
        }
        for (int k = 0; k < r.captureCount; ++k) {
            const PreludeCaptureRec& c = d.captures[static_cast<std::size_t>(r.captureFirst + k)];
            m->addCapture(c.parentSlot, c.localSlot);
        }
        if (r.paramCount > 0) m->setParamNames(stringsAt(r.paramFirst, r.paramCount));
        for (int k = 0; k < r.defaultCount; ++k) {
            const PreludeDefaultRec& dr = d.defaults[static_cast<std::size_t>(r.defaultFirst + k)];
            m->setDefaultBlock(dr.param, dr.block);
        }
        built[i] = std::move(m);
    }
    // Reattach the tree. BOTH halves of this are load-bearing, because a block
    // index is baked into every MAKE_FN operand:
    //   - a parent's children are added in ASCENDING index order, so each child
    //     lands at the index the compiler gave it;
    //   - parents are visited in DESCENDING order, so a child has already
    //     collected its own children before it is moved into its parent.
    // Adding children while walking the flat array backwards satisfies the
    // second and violates the first, which produces a prelude whose every
    // nested function is reachable under another function's index.
    std::vector<std::vector<std::size_t>> children(d.moduleCount);
    for (std::size_t i = 1; i < d.moduleCount; ++i) {
        const int parent = d.modules[i].parent;
        if (parent < 0 || static_cast<std::size_t>(parent) >= i)
            throw std::logic_error("prelude image: module " + std::to_string(i) +
                                   " has no enclosing module before it");
        children[static_cast<std::size_t>(parent)].push_back(i);
    }
    if (d.modules[0].parent >= 0) throw std::logic_error("prelude image: no top-level module");
    for (std::size_t i = d.moduleCount; i-- > 0;)
        for (std::size_t child : children[i]) built[i]->addBlock(std::move(built[child]));

    for (std::size_t i = 0; i < d.bindingCount; ++i) {
        const PreludeBindingRec& b = d.bindings[i];
        globals.bind(b.name, GlobalBinding{static_cast<BindingKind>(b.kind), b.key,
                                           masksAt(b.maskFirst, b.maskCount)});
    }
    for (std::size_t i = 0; i < d.selectorCount; ++i)
        globals.noteByNameSelector(d.selectors[i].name, {d.selectors[i].mask});
    for (std::size_t i = 0; i < d.typeCount; ++i) globals.defineType(classInfoOf(d.types[i]));
    for (std::size_t i = 0; i < d.typeAliasCount; ++i)
        globals.aliasType(d.typeAliases[i].name, d.typeAliases[i].key);

    modules.push_back(std::move(built[0]));
    return *modules.back();
}

}  // namespace protoScala

#endif  // PROTOSCALA_HAVE_PRELUDE_IMAGE
