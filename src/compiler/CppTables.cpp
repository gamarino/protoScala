#include "compiler/CppTables.h"

#include <cstdio>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace protoScala::tables {

void emitString(std::ostream& o, std::string_view s) {
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

std::string quoted(std::string_view s) {
    std::ostringstream o;
    emitString(o, s);
    return o.str();
}

std::string exactDouble(double d, std::string_view where) {
    if (!(d == d) || d == 1.0 / 0.0 || d == -1.0 / 0.0)
        throw std::runtime_error(std::string(where) +
                                 " holds a non-finite double literal, which a generated static "
                                 "table cannot represent exactly");
    std::ostringstream o;
    o << std::hexfloat << d;
    return o.str();
}

namespace {

void flattenInto(const BytecodeModule& m, int parent, std::vector<FlatBlock>& out) {
    const std::size_t self = out.size();
    out.push_back(FlatBlock{&m, parent, "blk" + std::to_string(self)});
    // Depth-first, children immediately after their parent, which is the order
    // BytecodeModule::addBlock created and therefore the order a MAKE_FN operand
    // means. The prelude image's reconstruction relies on the same invariant.
    for (std::size_t i = 0; i < m.blockCount(); ++i)
        flattenInto(m.block(i), static_cast<int>(self), out);
}

}  // namespace

std::vector<FlatBlock> flatten(const BytecodeModule& root) {
    std::vector<FlatBlock> out;
    flattenInto(root, -1, out);
    // The invariant, checked rather than assumed: every block reached from its
    // parent by index must be the block this flattening put at that position.
    for (std::size_t i = 0; i < out.size(); ++i) {
        const BytecodeModule& m = *out[i].mod;
        std::size_t child = i + 1;
        for (std::size_t b = 0; b < m.blockCount(); ++b) {
            if (child >= out.size() || out[child].mod != &m.block(b))
                throw std::logic_error("flatten: block " + std::to_string(b) + " of " + m.name() +
                                       " is not where a MAKE_FN operand would look for it");
            // Skip the whole subtree of that child.
            std::size_t depth = 1, k = child + 1;
            while (k < out.size() && out[k].parent >= static_cast<int>(child)) ++k;
            (void)depth;
            child = k;
        }
    }
    return out;
}

int StringPool::add(const std::vector<std::string>& v) {
    const int first = static_cast<int>(pool_.size());
    pool_.insert(pool_.end(), v.begin(), v.end());
    return first;
}

void emitConsts(std::ostream& o, const std::string& name, const BytecodeModule& mod,
                StringPool& pool) {
    o << "static const protoScala::gen::ConstRec " << name << "[] = {";
    if (mod.constCount() == 0) {
        // A zero-length array is not valid C++; the count stays 0.
        o << " {} ";
    } else {
        for (std::size_t i = 0; i < mod.constCount(); ++i) {
            const BytecodeModule::Const& c = mod.constAt(i);
            const int namesFirst = pool.add(c.names);
            const int fieldsFirst = pool.add(c.fields);
            o << "\n    /* " << i << " */ {"
              << ".kind = " << unsigned(static_cast<std::uint8_t>(c.kind))
              << ", .ival = " << c.ival << "LL"
              << ", .dval = " << exactDouble(c.dval, mod.name())
              << ", .base = " << c.base
              << ", .argc = " << c.argc << "u"
              << ", .flags = " << c.flags << "u"
              << ", .exact = " << (c.exact ? "true" : "false")
              << ", .sval = " << quoted(c.sval)
              << ", .slen = " << c.sval.size() << "u"
              << ", .key = " << quoted(c.key)
              << ", .namesFirst = " << namesFirst
              << ", .namesCount = " << c.names.size()
              << ", .fieldsFirst = " << fieldsFirst
              << ", .fieldsCount = " << c.fields.size()
              << "},";
        }
        o << '\n';
    }
    o << "};\n";
}

void emitHandlers(std::ostream& o, const std::string& name, const BytecodeModule& mod) {
    o << "static const protoScala::gen::HandlerRec " << name << "[] = {";
    if (mod.handlers().empty()) {
        o << " {} ";
    } else {
        // Table order, because table order is search order.
        for (const BytecodeModule::Handler& h : mod.handlers())
            o << "\n    {.startPc = " << h.startPc << "u, .endPc = " << h.endPc
              << "u, .handlerPc = " << h.handlerPc << "u, .stackDepth = " << h.stackDepth
              << ", .slot = " << h.slot << ", .kind = "
              << unsigned(static_cast<std::uint8_t>(h.kind)) << "},";
        o << '\n';
    }
    o << "};\n";
}

void emitStrings(std::ostream& o, const std::string& name, const StringPool& pool) {
    o << "static const char* const " << name << "[] = {";
    if (pool.all().empty()) {
        o << " \"\" ";
    } else {
        for (const std::string& s : pool.all()) o << "\n    " << quoted(s) << ',';
        o << '\n';
    }
    o << "};\n";
}

void emitSymbolArrays(std::ostream& o, const std::string& name, std::size_t constCount,
                      std::size_t stringCount) {
    // Filled once per process by gen::linkModule, which is what replaces
    // BytecodeModule::linkSymbols. They hold createSymbol symbols, which are
    // strong and never collected, so these arrays are not GC roots.
    o << "static const proto::ProtoString* " << name << "_symbols["
      << (constCount ? constCount : 1) << "] = {};\n"
      << "static const proto::ProtoString* " << name << "_keySymbols["
      << (constCount ? constCount : 1) << "] = {};\n"
      << "static const proto::ProtoString* " << name << "_stringSymbols["
      << (stringCount ? stringCount : 1) << "] = {};\n";
}

void emitBlockRec(std::ostream& o, const FlatBlock& blk, std::size_t index,
                  const std::vector<std::size_t>& childIndices, std::size_t stringCount) {
    const BytecodeModule& m = *blk.mod;
    const std::string n = blk.cppName;
    o << "static const protoScala::gen::BlockRec* const " << n << "_blocks[] = {";
    if (childIndices.empty()) {
        o << " nullptr ";
    } else {
        for (std::size_t c : childIndices) o << " &blk" << c << "_rec,";
    }
    o << "};\n";
    o << "static const int " << n << "_captureSlots[] = {";
    if (m.captureSpecs().empty()) {
        o << " 0 ";
    } else {
        for (const BytecodeModule::CaptureSpec& c : m.captureSpecs()) o << ' ' << c.localSlot << ',';
    }
    o << "};\n";
    // Filled once per process by gen::linkModule; the runtime uses it as this
    // block's identity.
    o << "static void* " << n << "_handle = nullptr;\n";
    o << "static const protoScala::gen::BlockRec " << n << "_rec = {\n"
      << "    .name = " << quoted(m.name()) << ",\n"
      << "    .arity = " << m.arity() << "u, .localCount = " << m.localCount()
      << "u, .maxStack = " << m.maxStack() << "u, .captureCount = " << m.captureCount() << "u,\n"
      << "    .variadic = " << (m.isVariadic() ? "true" : "false")
      << ", .method = " << (m.isMethod() ? "true" : "false")
      << ", .paramless = " << (m.isParamless() ? "true" : "false") << ",\n"
      << "    .entry = &" << n << ",\n"
      << "    .consts = " << n << "_consts, .constCount = " << m.constCount() << ",\n"
      << "    .handlers = " << n << "_handlers, .handlerCount = " << m.handlers().size() << ",\n"
      << "    .strings = " << n << "_strings, .stringCount = " << stringCount << ",\n"
      << "    .symbols = " << n << "_symbols,\n"
      << "    .keySymbols = " << n << "_keySymbols,\n"
      << "    .stringSymbols = " << n << "_stringSymbols,\n"
      << "    .blocks = " << n << "_blocks, .blockCount = " << childIndices.size() << ",\n"
      << "    .captureSlots = " << n << "_captureSlots,\n"
      << "    .handle = &" << n << "_handle,\n"
      << "};\n";
    (void)index;
}

}  // namespace protoScala::tables
