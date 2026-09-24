/*
 * ModuleLoader — the compile-time seam between the Compiler and the session
 * that owns the ProtoSpace (Phase 6 plan A0-1).
 *
 * The Compiler asks for a module by name and receives PLAIN C++ descriptors.
 * No ProtoObject* crosses this interface, so DESIGN §3.3 holds: the AST and the
 * compiler never hold a protoCore pointer. Everything that needs a ProtoContext
 * happens on the other side, in Session.
 *
 * This is the one place in the dialect that binds EARLY rather than late, and it
 * is deliberate: a Scala programmer imports TYPES (`import util.Shapes.Point`
 * then `case Point(x, y) =>`), and a name bound at run time carries no
 * ClassInfo, so `new Point(1, 2)` and `case p: Point` would not compile. Late
 * binding is preserved everywhere it is observable — a MEMBER of an imported
 * module is still reached by an ordinary SEND, and a foreign module's members
 * resolve by name at run time.
 */
#pragma once
#include "compiler/ClassInfo.h"
#include "compiler/GlobalTable.h"
#include "frontend/AST.h"

#include <string>
#include <utility>
#include <vector>

namespace protoScala {

// What one loaded module contributes to an importing unit's tables.
struct ModuleExports {
    std::string moduleName;     // the name the module binds by default ("Strings", "numpy")
    std::string moduleKey;      // the session global that holds the module object
    std::string moduleTypeKey;  // a Scala module: its synthetic object's type key; "" foreign
    BindingKind moduleKind = BindingKind::Object;  // Object (Scala) or Val (foreign)
    bool foreign = false;
    // A Scala module's declared names, qualified as the module spells them
    // ("Shapes.Point"), each with the binding its own GlobalTable gave it.
    std::vector<std::pair<std::string, GlobalBinding>> terms;
    // A Scala module's declared types, keyed by the same qualified names.
    std::vector<std::pair<std::string, ClassInfo>> types;
    // The conservative by-name selector index the module's own compilation
    // built: without it a call to an imported def with a by-name parameter
    // would be under-boxed in the importing unit.
    std::vector<std::pair<std::string, std::uint32_t>> byNameSelectors;
};

class ModuleLoader {
public:
    virtual ~ModuleLoader() = default;
    // Loads a module. `providerSpec` is "" for the resolution chain or
    // "provider:<alias>" for one named provider; `logicalPath` is dotted with
    // the family prefix already stripped; `importerDir` is the directory of the
    // file that wrote the import, "" in the REPL. The reference stays valid for
    // the life of the session. Throws CompileError, positioned at `pos`, on a
    // miss, a cycle, or a failure inside the module.
    virtual const ModuleExports& load(const std::string& providerSpec,
                                      const std::string& logicalPath,
                                      const std::string& importerDir, SourcePos pos) = 0;
    // Reads one attribute of an already-loaded FOREIGN module and binds it to a
    // fresh session global; returns that global's key. Throws CompileError when
    // the attribute is absent — told apart from a `null` attribute with
    // hasAttribute, never by comparing with PROTO_NONE, because PROTO_NONE is
    // both Scala `null` and "attribute missing".
    virtual std::string bindForeignMember(const ModuleExports& mod, const std::string& name,
                                          SourcePos pos) = 0;
};

} // namespace protoScala
