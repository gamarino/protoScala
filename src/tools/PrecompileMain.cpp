/*
 * protoscala-precompile — a BUILD-TIME host tool. It reads lib/prelude.scala,
 * runs the same lexer, parser, desugarer and compiler the runtime would, and
 * writes PreludeImage.cpp: static tables that src/runtime/PreludeImage.cpp
 * walks to reconstruct the BytecodeModule tree and the GlobalTable, with no
 * lexer, parser, desugarer or compiler in the path.
 *
 * It links protoscala_compiler and protoscala_support only. It never creates a
 * ProtoSpace and never calls linkSymbols: the image is space-independent, and
 * the tables hold strings, integers and PODs — no ProtoObject* and no address.
 *
 * Usage: protoscala-precompile <prelude.scala> <out.cpp>
 */
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "support/BuiltinNames.h"
#include "support/PreludeImage.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <utility>
#include <sstream>
#include <string>
#include <vector>

using namespace protoScala;

namespace {

// --- Emission helpers ------------------------------------------------------

void emitString(std::ostream& o, const std::string& s) {
    o << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o << '\\' << static_cast<char>(c);
        else if (c == '\n') o << "\\n";
        else if (c == '\t') o << "\\t";
        else if (c == '\r') o << "\\r";
        else if (c == '?') o << "\\?";              // never start a trigraph
        else if (c >= 0x20 && c < 0x7f) o << static_cast<char>(c);
        else { char b[16]; std::snprintf(b, sizeof b, "\"\"\\x%02x\"\"", c); o << b; }
    }
    o << '"';
}

std::string quoted(const std::string& s) {
    std::ostringstream o;
    emitString(o, s);
    return o.str();
}

// A double as a C++ literal that round-trips bit for bit. A hexadecimal
// floating literal (C++17) is exact; a non-finite value would not be, so it is
// refused rather than silently approximated.
std::string exactDouble(double d) {
    if (!(d == d) || d == 1.0 / 0.0 || d == -1.0 / 0.0)
        throw std::runtime_error("the prelude holds a non-finite double literal, "
                                 "which the image format cannot represent exactly");
    std::ostringstream o;
    o << std::hexfloat << d;
    return o.str();
}

// The tables being built. Every *Pool is shared by index ranges.
struct Tables {
    std::vector<std::uint32_t> code;
    std::vector<int> lines;
    std::vector<std::string> strings;         // contiguous ranges, never de-duplicated
    std::vector<std::uint32_t> masks;
    std::vector<std::uint32_t> auxArities;
    std::vector<PreludeConstRec> consts;
    std::vector<std::string> constSval, constKey;   // parallel to consts (owns the text)
    std::vector<PreludeHandlerRec> handlers;
    std::vector<PreludeCaptureRec> captures;
    std::vector<PreludeDefaultRec> defaults;
    std::vector<PreludeModuleRec> modules;
    std::vector<std::string> moduleName;             // parallel to modules
    std::vector<PreludeBindingRec> bindings;
    std::vector<std::string> bindingName, bindingKey;
    std::vector<PreludeMemberRec> members;
    std::vector<std::string> memberName, memberKey;
    std::vector<PreludeTypeRec> types;
    std::vector<std::string> typeName, typeKey, typeCompTerm, typeCompType;
    std::vector<std::pair<std::string, std::string>> typeAliases;

    int addStrings(const std::vector<std::string>& v) {
        const int first = static_cast<int>(strings.size());
        strings.insert(strings.end(), v.begin(), v.end());
        return first;
    }
    int addMasks(const std::vector<std::uint32_t>& v) {
        const int first = static_cast<int>(masks.size());
        masks.insert(masks.end(), v.begin(), v.end());
        return first;
    }
};

// --- Flattening the module tree -------------------------------------------

void flatten(const BytecodeModule& m, int parent, Tables& t) {
    const std::size_t self = t.modules.size();
    t.modules.emplace_back();
    t.moduleName.push_back(m.name());

    PreludeModuleRec r;
    r.parent = parent;
    r.arity = m.arity();
    r.variadic = m.isVariadic();
    r.method = m.isMethod();
    r.paramless = m.isParamless();
    r.localCount = m.localCount();
    r.maxStack = m.maxStack();

    r.codeFirst = static_cast<int>(t.code.size());
    for (std::size_t i = 0; i < m.code().size(); ++i) {
        t.code.push_back(m.code()[i]);
        t.lines.push_back(m.lineAt(i));
    }
    r.codeCount = static_cast<int>(m.code().size());

    r.constFirst = static_cast<int>(t.consts.size());
    for (std::size_t i = 0; i < m.constCount(); ++i) {
        const BytecodeModule::Const& c = m.constAt(i);
        PreludeConstRec cr;
        cr.kind = static_cast<std::uint8_t>(c.kind);
        cr.ival = c.ival;
        cr.dval = c.dval;
        cr.base = c.base;
        cr.argc = c.argc;
        cr.flags = c.flags;
        cr.exact = c.exact;
        cr.namesFirst = t.addStrings(c.names);
        cr.namesCount = static_cast<int>(c.names.size());
        cr.fieldsFirst = t.addStrings(c.fields);
        cr.fieldsCount = static_cast<int>(c.fields.size());
        t.consts.push_back(cr);
        t.constSval.push_back(c.sval);
        t.constKey.push_back(c.key);
    }
    r.constCount = static_cast<int>(m.constCount());

    r.handlerFirst = static_cast<int>(t.handlers.size());
    for (const BytecodeModule::Handler& h : m.handlers())
        t.handlers.push_back(PreludeHandlerRec{
            static_cast<std::uint32_t>(h.startPc), static_cast<std::uint32_t>(h.endPc),
            static_cast<std::uint32_t>(h.handlerPc), h.stackDepth, h.slot,
            static_cast<std::uint8_t>(h.kind)});
    r.handlerCount = static_cast<int>(m.handlers().size());

    r.captureFirst = static_cast<int>(t.captures.size());
    for (const BytecodeModule::CaptureSpec& c : m.captureSpecs())
        t.captures.push_back(PreludeCaptureRec{c.parentSlot, c.localSlot});
    r.captureCount = static_cast<int>(m.captureSpecs().size());

    r.paramFirst = t.addStrings(m.paramNames());
    r.paramCount = static_cast<int>(m.paramNames().size());

    r.defaultFirst = static_cast<int>(t.defaults.size());
    for (std::size_t p = 0; p < m.paramNames().size(); ++p) {
        const std::size_t b = m.defaultBlock(p);
        if (b != BytecodeModule::kNoDefault)
            t.defaults.push_back(PreludeDefaultRec{static_cast<std::uint32_t>(p),
                                                   static_cast<std::uint32_t>(b)});
    }
    r.defaultCount = static_cast<int>(t.defaults.size()) - r.defaultFirst;

    t.modules[self] = r;

    // Depth-first, children immediately after their parent. The reconstruction
    // adds each child to its parent in index order, so every block index baked
    // into a MAKE_FN operand stays exactly what the compiler emitted.
    for (std::size_t i = 0; i < m.blockCount(); ++i)
        flatten(m.block(i), static_cast<int>(self), t);
}

// --- The GlobalTable -------------------------------------------------------

void collectGlobals(const GlobalTable& g, Tables& t) {
    std::vector<std::string> names;
    for (const auto& kv : g.allBindings()) names.push_back(kv.first);
    std::sort(names.begin(), names.end());  // a reproducible generated file
    for (const std::string& n : names) {
        const GlobalBinding& b = g.allBindings().at(n);
        PreludeBindingRec r;
        r.kind = static_cast<std::uint8_t>(b.kind);
        r.maskFirst = t.addMasks(b.byNameMasks);
        r.maskCount = static_cast<int>(b.byNameMasks.size());
        t.bindings.push_back(r);
        t.bindingName.push_back(n);
        t.bindingKey.push_back(b.key);
    }

    // The type namespace, verbatim: one (name, key) pair per entry, sorted so
    // the generated file is reproducible. A ClassInfo's own `name` is a
    // different thing (`object Try`'s class is named `Try`, its namespace entry
    // is `Try.type`), so the two are never conflated.
    {
        std::vector<std::string> names;
        for (const auto& kv : g.typeKeys()) names.push_back(kv.first);
        std::sort(names.begin(), names.end());
        for (const std::string& n : names) t.typeAliases.emplace_back(n, g.typeKeys().at(n));
    }

    std::vector<std::string> keys;
    for (const auto& kv : g.allTypes()) keys.push_back(kv.first);
    std::sort(keys.begin(), keys.end());
    for (const std::string& k : keys) {
        const ClassInfo& c = g.allTypes().at(k);
        PreludeTypeRec r;
        r.kind = static_cast<std::uint8_t>(c.kind);
        r.flags = (c.isCase ? kPTCase : 0u) | (c.isAbstract ? kPTAbstract : 0u) |
                  (c.isFinal ? kPTFinal : 0u) | (c.isSealed ? kPTSealed : 0u) |
                  (c.builtin ? kPTBuiltin : 0u) |
                  (c.mutableInstances ? kPTMutableInstances : 0u) |
                  (c.hasInit ? kPTHasInit : 0u) |
                  (c.primaryVariadic ? kPTPrimaryVariadic : 0u) |
                  (c.companionHasApply ? kPTCompanionHasApply : 0u) |
                  (c.companionHasUnapply ? kPTCompanionHasUnapply : 0u);
        r.linFirst = t.addStrings(c.linearization);
        r.linCount = static_cast<int>(c.linearization.size());
        r.fieldFirst = t.addStrings(c.fields);
        r.fieldCount = static_cast<int>(c.fields.size());
        r.ctorFirst = t.addStrings(c.ctorParams);
        r.ctorCount = static_cast<int>(c.ctorParams.size());
        r.auxFirst = static_cast<int>(t.auxArities.size());
        for (std::size_t a : c.auxArities) t.auxArities.push_back(static_cast<std::uint32_t>(a));
        r.auxCount = static_cast<int>(c.auxArities.size());
        r.primaryMaskFirst = t.addMasks(c.primaryByNameMasks);
        r.primaryMaskCount = static_cast<int>(c.primaryByNameMasks.size());
        r.primaryArity = static_cast<std::uint32_t>(c.primaryArity);
        r.primaryMinArity = static_cast<std::uint32_t>(c.primaryMinArity);

        std::vector<std::string> memberNames;
        for (const auto& kv : c.members) memberNames.push_back(kv.first);
        std::sort(memberNames.begin(), memberNames.end());
        r.memberFirst = static_cast<int>(t.members.size());
        for (const std::string& mn : memberNames) {
            const MemberInfo& mi = c.members.at(mn);
            PreludeMemberRec mr;
            mr.kind = static_cast<std::uint8_t>(mi.kind);
            mr.concrete = mi.concrete;
            mr.byNameValue = mi.byNameValue;
            mr.maskFirst = t.addMasks(mi.byNameMasks);
            mr.maskCount = static_cast<int>(mi.byNameMasks.size());
            t.members.push_back(mr);
            t.memberName.push_back(mn);
            t.memberKey.push_back(mi.key);
        }
        r.memberCount = static_cast<int>(memberNames.size());

        t.types.push_back(r);
        t.typeName.push_back(c.name);
        t.typeKey.push_back(k);
        t.typeCompTerm.push_back(c.companionTermKey);
        t.typeCompType.push_back(c.companionTypeKey);
    }
}

// --- Writing the file ------------------------------------------------------

template <typename T, typename F>
void emitArray(std::ostream& o, const char* type, const char* name, const std::vector<T>& v,
               F&& one) {
    o << "const " << type << ' ' << name << "[] = {";
    if (v.empty()) {
        // A zero-length array is not valid C++; the span's count stays 0.
        o << " {} ";
    } else {
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i % 4 == 0) o << "\n    ";
            one(o, v[i], i);
            o << ',';
        }
        o << '\n';
    }
    o << "};\n";
}

bool emitImage(const char* path, const std::string& source, const GlobalTable& globals,
               const BytecodeModule& top) {
    Tables t;
    flatten(top, -1, t);
    collectGlobals(globals, t);

    std::ofstream o(path, std::ios::binary);
    if (!o) {
        std::fprintf(stderr, "protoscala-precompile: cannot write %s\n", path);
        return false;
    }
    o << "// Generated by protoscala-precompile from lib/prelude.scala; do not edit.\n"
      << "// The reconstruction that walks these tables is hand-written and lives in\n"
      << "// src/runtime/PreludeImage.cpp, so it cannot drift per prelude.\n"
      << "#include \"support/PreludeImage.h\"\n\n"
      << "namespace protoScala {\nnamespace {\n\n";

    o << "const std::uint32_t kCode[] = {";
    for (std::size_t i = 0; i < t.code.size(); ++i) {
        if (i % 8 == 0) o << "\n    ";
        o << "0x" << std::hex << t.code[i] << std::dec << "u,";
    }
    if (t.code.empty()) o << " 0u ";
    o << "\n};\n";

    o << "const int kLines[] = {";
    for (std::size_t i = 0; i < t.lines.size(); ++i) {
        if (i % 16 == 0) o << "\n    ";
        o << t.lines[i] << ',';
    }
    if (t.lines.empty()) o << " 0 ";
    o << "\n};\n";

    o << "const char* const kStrings[] = {";
    for (std::size_t i = 0; i < t.strings.size(); ++i) {
        o << "\n    " << quoted(t.strings[i]) << ',';
    }
    if (t.strings.empty()) o << " \"\" ";
    o << "\n};\n";

    o << "const std::uint32_t kMasks[] = {";
    for (std::size_t i = 0; i < t.masks.size(); ++i) {
        if (i % 8 == 0) o << "\n    ";
        o << "0x" << std::hex << t.masks[i] << std::dec << "u,";
    }
    if (t.masks.empty()) o << " 0u ";
    o << "\n};\n";

    o << "const std::uint32_t kAux[] = {";
    for (std::size_t i = 0; i < t.auxArities.size(); ++i) o << ' ' << t.auxArities[i] << "u,";
    if (t.auxArities.empty()) o << " 0u ";
    o << "\n};\n";

    emitArray(o, "PreludeConstRec", "kConsts", t.consts,
              [&](std::ostream& s, const PreludeConstRec& c, std::size_t i) {
                  s << "{." << "kind = " << unsigned(c.kind) << ", .ival = " << c.ival << "LL"
                    << ", .dval = " << exactDouble(c.dval)
                    << ", .base = " << c.base << ", .argc = " << c.argc << "u"
                    << ", .flags = " << c.flags << "u"
                    << ", .exact = " << (c.exact ? "true" : "false")
                    << ", .sval = " << quoted(t.constSval[i])
                    << ", .key = " << quoted(t.constKey[i])
                    << ", .namesFirst = " << c.namesFirst << ", .namesCount = " << c.namesCount
                    << ", .fieldsFirst = " << c.fieldsFirst << ", .fieldsCount = " << c.fieldsCount
                    << "}";
              });

    emitArray(o, "PreludeHandlerRec", "kHandlers", t.handlers,
              [](std::ostream& s, const PreludeHandlerRec& h, std::size_t) {
                  s << "{.startPc = " << h.startPc << "u, .endPc = " << h.endPc
                    << "u, .handlerPc = " << h.handlerPc << "u, .stackDepth = " << h.stackDepth
                    << ", .slot = " << h.slot << ", .kind = " << unsigned(h.kind) << "}";
              });

    emitArray(o, "PreludeCaptureRec", "kCaptures", t.captures,
              [](std::ostream& s, const PreludeCaptureRec& c, std::size_t) {
                  s << "{.parentSlot = " << c.parentSlot << ", .localSlot = " << c.localSlot << "}";
              });

    emitArray(o, "PreludeDefaultRec", "kDefaults", t.defaults,
              [](std::ostream& s, const PreludeDefaultRec& d, std::size_t) {
                  s << "{.param = " << d.param << "u, .block = " << d.block << "u}";
              });

    emitArray(o, "PreludeModuleRec", "kModules", t.modules,
              [&](std::ostream& s, const PreludeModuleRec& m, std::size_t i) {
                  s << "{.parent = " << m.parent << ", .name = " << quoted(t.moduleName[i])
                    << ", .arity = " << m.arity
                    << ", .variadic = " << (m.variadic ? "true" : "false")
                    << ", .method = " << (m.method ? "true" : "false")
                    << ", .paramless = " << (m.paramless ? "true" : "false")
                    << ", .localCount = " << m.localCount << ", .maxStack = " << m.maxStack
                    << ", .codeFirst = " << m.codeFirst << ", .codeCount = " << m.codeCount
                    << ", .constFirst = " << m.constFirst << ", .constCount = " << m.constCount
                    << ", .handlerFirst = " << m.handlerFirst
                    << ", .handlerCount = " << m.handlerCount
                    << ", .captureFirst = " << m.captureFirst
                    << ", .captureCount = " << m.captureCount
                    << ", .paramFirst = " << m.paramFirst << ", .paramCount = " << m.paramCount
                    << ", .defaultFirst = " << m.defaultFirst
                    << ", .defaultCount = " << m.defaultCount << "}";
              });

    emitArray(o, "PreludeBindingRec", "kBindings", t.bindings,
              [&](std::ostream& s, const PreludeBindingRec& b, std::size_t i) {
                  s << "{.name = " << quoted(t.bindingName[i]) << ", .kind = " << unsigned(b.kind)
                    << ", .key = " << quoted(t.bindingKey[i]) << ", .maskFirst = " << b.maskFirst
                    << ", .maskCount = " << b.maskCount << "}";
              });

    emitArray(o, "PreludeMemberRec", "kMembers", t.members,
              [&](std::ostream& s, const PreludeMemberRec& m, std::size_t i) {
                  s << "{.name = " << quoted(t.memberName[i]) << ", .kind = " << unsigned(m.kind)
                    << ", .key = " << quoted(t.memberKey[i])
                    << ", .concrete = " << (m.concrete ? "true" : "false")
                    << ", .byNameValue = " << (m.byNameValue ? "true" : "false")
                    << ", .maskFirst = " << m.maskFirst << ", .maskCount = " << m.maskCount << "}";
              });

    emitArray(o, "PreludeTypeRec", "kTypes", t.types,
              [&](std::ostream& s, const PreludeTypeRec& c, std::size_t i) {
                  s << "{.name = " << quoted(t.typeName[i]) << ", .key = " << quoted(t.typeKey[i])
                    << ", .kind = " << unsigned(c.kind) << ", .flags = " << c.flags << "u"
                    << ", .linFirst = " << c.linFirst << ", .linCount = " << c.linCount
                    << ", .fieldFirst = " << c.fieldFirst << ", .fieldCount = " << c.fieldCount
                    << ", .ctorFirst = " << c.ctorFirst << ", .ctorCount = " << c.ctorCount
                    << ", .auxFirst = " << c.auxFirst << ", .auxCount = " << c.auxCount
                    << ", .primaryMaskFirst = " << c.primaryMaskFirst
                    << ", .primaryMaskCount = " << c.primaryMaskCount
                    << ", .memberFirst = " << c.memberFirst
                    << ", .memberCount = " << c.memberCount
                    << ", .primaryArity = " << c.primaryArity << "u"
                    << ", .primaryMinArity = " << c.primaryMinArity << "u"
                    << ", .companionTermKey = " << quoted(t.typeCompTerm[i])
                    << ", .companionTypeKey = " << quoted(t.typeCompType[i]) << "}";
              });

    emitArray(o, "PreludeTypeAliasRec", "kTypeAliases", t.typeAliases,
              [](std::ostream& s, const std::pair<std::string, std::string>& a, std::size_t) {
                  s << "{.name = " << quoted(a.first) << ", .key = " << quoted(a.second) << "}";
              });

    // The by-name selector index (CaptureAnalysis's conservative union). Without
    // it a call to a prelude def with a by-name parameter would be under-boxed,
    // and a missing Cell is not harmless where an extra one is.
    o << "const PreludeSelectorRec kSelectors[] = {";
    {
        std::vector<std::string> sel;
        for (const auto& kv : globals.byNameSelectors()) sel.push_back(kv.first);
        std::sort(sel.begin(), sel.end());
        for (const std::string& s : sel)
            o << "\n    {.name = " << quoted(s) << ", .mask = "
              << globals.byNameSelectors().at(s) << "u},";
        if (sel.empty()) o << " {} ";
    }
    o << "\n};\n";

    o << "\nconst PreludeImageData kData = {\n"
      << "    .format = " << kPreludeImageFormat << "u,\n"
      << "    .sourceHash = " << fnv1a64(source.data(), source.size()) << "ull,\n"
      << "    .code = kCode, .codeCount = " << t.code.size() << ",\n"
      << "    .lines = kLines, .lineCount = " << t.lines.size() << ",\n"
      << "    .strings = kStrings, .stringCount = " << t.strings.size() << ",\n"
      << "    .masks = kMasks, .maskCount = " << t.masks.size() << ",\n"
      << "    .auxArities = kAux, .auxArityCount = " << t.auxArities.size() << ",\n"
      << "    .consts = kConsts, .constCount = " << t.consts.size() << ",\n"
      << "    .handlers = kHandlers, .handlerCount = " << t.handlers.size() << ",\n"
      << "    .captures = kCaptures, .captureCount = " << t.captures.size() << ",\n"
      << "    .defaults = kDefaults, .defaultCount = " << t.defaults.size() << ",\n"
      << "    .modules = kModules, .moduleCount = " << t.modules.size() << ",\n"
      << "    .bindings = kBindings, .bindingCount = " << t.bindings.size() << ",\n"
      << "    .members = kMembers, .memberCount = " << t.members.size() << ",\n"
      << "    .types = kTypes, .typeCount = " << t.types.size() << ",\n"
      << "    .typeAliases = kTypeAliases, .typeAliasCount = " << t.typeAliases.size() << ",\n"
      << "    .selectors = kSelectors, .selectorCount = " << globals.byNameSelectors().size()
      << ",\n};\n\n"
      << "}  // namespace\n\n"
      << "const PreludeImageData& preludeImageData() { return kData; }\n\n"
      << "}  // namespace protoScala\n";
    return o.good();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: protoscala-precompile <prelude.scala> <out.cpp>\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "protoscala-precompile: cannot read %s\n", argv[1]);
        return 1;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string source = buf.str();

    // Exactly the table a Session has when it calls loadPrelude: the builtin
    // globals, their by-name signatures and the builtin types, in that order.
    GlobalTable globals;
    for (const auto& n : builtinGlobalNames()) globals.declare(n, BindingKind::Builtin);
    for (const BuiltinByNameSignature& sig : builtinByNameSignatures())
        globals.setByNameMasks(sig.global, sig.applyMasks);
    for (ClassInfo& t : builtinTypes()) globals.defineBuiltinType(std::move(t));

    CompiledUnit cu;
    try {
        std::unique_ptr<CompilationUnit> unit = parseSource(source);
        desugar(*unit);
        Compiler compiler(globals);
        cu = compiler.compileUnit(*unit, UnitMode::Script, 0);
    } catch (const ParseError& e) {
        std::fprintf(stderr, "%s:%d:%d: error: %s\n", argv[1], e.pos.line, e.pos.column, e.what());
        return 1;
    } catch (const CompileError& e) {
        std::fprintf(stderr, "%s:%d:%d: error: %s\n", argv[1], e.pos.line, e.pos.column, e.what());
        return 1;
    }
    return emitImage(argv[2], source, globals, *cu.module) ? 0 : 1;
}
