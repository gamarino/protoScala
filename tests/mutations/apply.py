#!/usr/bin/env python3
"""Apply one named mutation of the transpiler, or restore. Phase 7 Task 12."""
import pathlib, sys, shutil

ROOT = pathlib.Path('/home/gamarino/Documentos/proyectos/protoScala')
BAK = pathlib.Path('/home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler/mutbak')
FILES = ['src/compiler/CppEmitter.cpp', 'src/runtime/GeneratedSupport.cpp',
         'src/umd/ForeignBoundary.h', 'src/compiler/CppTables.cpp',
         'tests/interop/Exports.scala', 'src/umd/CompiledModuleProvider.cpp']

MUTS = {
 # id: (file, old, new)
 'E1': ('src/compiler/CppEmitter.cpp',
        '''              : d.op == Op::GE  ? "ge"  : d.op == Op::EQ ? "eq" : "ne";''',
        '''              : d.op == Op::GE  ? "ge"  : d.op == Op::EQ ? "eq" : "ne";
                if (d.op == Op::ADD) f = "sub";'''),
 'E2': ('src/runtime/GeneratedSupport.cpp',
        '''    return ops::sendNamed(ctx, engineOf("send"), base, blk.symbols[siteIdx], argc,
                          blk.keySymbols[siteIdx], /*applied=*/false);''',
        '''    return ops::sendNamed(ctx, engineOf("send"), base, blk.symbols[siteIdx], argc,
                          nullptr, /*applied=*/false);'''),
 'E3': ('src/compiler/CppEmitter.cpp',
        '''                     << (nc ? "&" + st(dep - nc) : "nullptr") << ", " << nc << ");";''',
        '''                     << (nc ? "&" + st(dep - nc) : "nullptr") << ", " << (nc ? nc - 1 : 0)
                     << ");";'''),
 'E6': ('src/compiler/CppEmitter.cpp',
        '''                out_ << "gen::safepoint(C); goto L" << (d.next - d.operand) << ";";''',
        '''                out_ << "goto L" << (d.next - d.operand) << ";";'''),
 'E7': ('src/umd/ForeignBoundary.h',
        '''    catch (const std::logic_error&) { throw; }   // D74: a VM defect stays uncatchable
''',
        ''),
 'E9': ('src/runtime/GeneratedSupport.cpp',
        '''                    blk.symbols[k] = proto::ProtoString::createSymbol(ctx, c.sval ? c.sval : "");''',
        '''                    blk.symbols[k] = proto::ProtoString::fromUTF8String(ctx, c.sval ? c.sval : "");'''),
 'E10': ('src/runtime/GeneratedSupport.cpp',
        '''    return ops::unapplyFieldsWith(ctx, blk.stringSymbols + c.namesFirst,
                                  static_cast<unsigned>(c.namesCount), v, out);''',
        '''    return ops::unapplyFieldsWith(ctx, blk.stringSymbols + c.namesFirst,
                                  static_cast<unsigned>(c.namesCount ? c.namesCount - 1 : 0), v,
                                  out);'''),
 'E11': ('src/compiler/CppEmitter.cpp',
        '''            case Op::PUSH_LOCAL:
                out_ << st(dep) << " = S[" << d.operand << "];";''',
        '''            case Op::PUSH_LOCAL:
                out_ << st(dep) << " = " << st(static_cast<int>(d.operand)) << ";";'''),
 'E13': ('src/compiler/CppEmitter.cpp',
        '''                out_ << st(dep - nn - 1) << " = gen::call(C, " << st(dep - nn - 1) << ", &"
                     << st(dep - nn) << ", " << nn << ");";''',
        '''                out_ << st(dep - nn - 1) << " = gen::call(C, " << st(dep - nn - 1) << ", &"
                     << st(dep - nn) << ", " << (nn ? nn - 1 : 0) << ");";'''),
 'E14': ('src/runtime/GeneratedSupport.cpp',
        '''        const proto::ProtoList* caps = self->asList(&frame_);
        for (std::uint32_t k = 0; k < blk.captureCount; ++k)
            slots_[blk.captureSlots[k]] = caps->getAt(&frame_, static_cast<int>(k));''',
        '''        (void)self;'''),
 'E15': ('src/runtime/GeneratedSupport.cpp',
        '''        for (unsigned k = 0; k < count; ++k)
            t[k] = args->getAt(&tail, static_cast<int>(first + k));''',
        '''        for (unsigned k = 0; k + 1 < count; ++k)
            t[k] = args->getAt(&tail, static_cast<int>(first + k));'''),
 'E16': ('src/compiler/CppEmitter.cpp',
        '''                out_ << "if (!gen::truthy(C, " << st(dep - 1) << ")) goto L"''',
        '''                out_ << "if (gen::truthy(C, " << st(dep - 1) << ")) goto L"'''),

 # --- the cross-runtime call (interop/foreign-call) ------------------------------
 # The claim under test is that a foreign runtime can call a transpiled function with
 # protoCore alone. Each of these four breaks one link of that chain, and each must
 # turn interop/foreign-call red; a green run here would mean the test asserts
 # nothing.
 'X1': ('src/compiler/CppEmitter.cpp',   # no exports are found at all
        '''        if (fn.captureCount() != 0) continue;   // see the declaration''',
        '''        if (fn.captureCount() != 0) continue;   // see the declaration
        if (true) continue;'''),
 'X2': ('src/runtime/GeneratedSupport.cpp',   # the host fallback for a foreign entry
        '''        if (!engine || !layout) noActiveContext("enterMethod");
        ExecutionEngine::ActiveCallGuard active(engine, layout);
        return body(ctx, self, pl, args, kwargs);''',
        '''        if (!engine || !layout) noActiveContext("enterMethod");
        return body(ctx, self, pl, args, kwargs);'''),
 'X3': ('src/runtime/GeneratedSupport.cpp',   # the cells are never attached
        '''        for (std::size_t k = 0; k < exportCount; ++k) {
            const auto* key = proto::ProtoString::createSymbol(&scope, exports[k].name);
            mod->setAttribute(&scope, key, scope.fromMethod(nullptr, exports[k].entry));
        }''',
        '''        (void)exports; (void)exportCount;'''),
 'X4': ('tests/interop/Exports.scala',   # the pinned values bite, on BOTH paths
        '''def add(a: Int, b: Int): Int = a + b''',
        '''def add(a: Int, b: Int): Int = a + b + 1'''),

 # --- CompiledModuleProvider (cli/compiled-provider) -----------------------------
 # Four properties, four mutations. Each must turn cli/compiled-provider red.
 'P1': ('src/umd/CompiledModuleProvider.cpp',   # the provider never finds anything
        '''    if (found.empty()) return PROTO_NONE;''',
        '''    if (found.empty()) return PROTO_NONE;
    return PROTO_NONE;'''),
 # P2 is the one that found a hole, and the hole is worth keeping in view: it left
 # cli/compiled-provider GREEN, because `Session::load` consults the source loader
 # itself and reaches the resolution chain only on a miss -- so protoScala's own
 # importer never observes the order. The order still decides for every OTHER runtime,
 # which arrives through protoCore's getImportModule, and
 # Provider.CompiledComesAfterSourceInTheChain now asserts it on the data. That test is
 # what this mutation reds.
 'P2': ('src/umd/CompiledModuleProvider.cpp',   # chain order: source must still win
        '''            if (s == "provider:scala") at = i + 1;''',
        '''            if (s == "provider:scala") at = 0;'''),
 'P3': ('src/umd/CompiledModuleProvider.cpp',   # D8: a script is not a module
        '''        if (::dlsym(handle, "proto_module_main")) {''',
        '''        if (false) {'''),
 'P4': ('src/umd/CompiledModuleProvider.cpp',   # a logical path is not a file path
        '''    std::replace(out.begin(), out.end(), '.', '/');''',
        '''    // mutated: the dots are left alone'''),

 # --- the frame's retry loop, D120 (Task 8) --------------------------------------
 # Four properties whose order or presence is load-bearing, and each has a test that
 # reds. R1 and R4 are the two that would otherwise produce a WRONG ANSWER rather than
 # an error, which is why they exist.
 'R1': ('src/runtime/GeneratedSupport.cpp',   # the D74 arm swallows its own subclasses
        '''    } catch (const std::invalid_argument& e) {''',
        '''    } catch (const std::logic_error&) {
        throw;   // mutated: moved AHEAD of the two subclasses the interpreter translates
    } catch (const std::invalid_argument& e) {'''),
 'R2': ('src/runtime/GeneratedSupport.cpp',   # the value is not re-rooted before use
        '''        ctx->returnValue = t.value;
        slots[pendingSlot] = t.value;
        slots[h->slot] = t.value;''',
        '''        slots[h->slot] = t.value;'''),
 'R3': ('src/compiler/CppEmitter.cpp',   # no pc tracking: the search reads a stale pc
        '''        if (guarded && canThrow(d.op)) out_ << "    pc = " << d.pc << ";\\n";''',
        '''        if (false) out_ << "    pc = " << d.pc << ";\\n";'''),
 'R4': ('src/runtime/GeneratedSupport.cpp',   # the range is ignored: the first entry wins
        '''        if (pc >= h.startPc && pc < h.endPc) return &h;''',
        '''        (void)pc; return &h;'''),
}


# The ctest expression that MUST go red for each mutation.
#
# This map exists because choosing the filter by hand went wrong: `Guarded.` matched
# nothing, since `test_generated_support` is registered as the single ctest case
# `unit/generated_support` rather than through gtest_discover_tests, so two mutations
# reported GREEN while the tests that cover them were never run. A mutation matrix whose
# target set is chosen by eye measures the eye. `apply.py covers <id>` prints the
# expression; the runner uses it instead of guessing.
COVERS = {
 'E1': 'transpiled/', 'E2': 'transpiled/', 'E3': 'transpiled/', 'E6': 'transpiled/',
 'E7': 'transpiled/', 'E9': 'transpiled/', 'E10': 'transpiled/', 'E11': 'transpiled/',
 'E13': 'transpiled/', 'E14': 'transpiled/', 'E15': 'transpiled/', 'E16': 'transpiled/',
 'X1': 'interop/foreign-call', 'X2': 'interop/foreign-call',
 'X3': 'interop/foreign-call', 'X4': 'interop/foreign-call',
 'P1': 'cli/compiled-provider', 'P2': 'unit/Provider|Provider\\.',
 'P3': 'cli/compiled-provider', 'P4': 'cli/compiled-provider',
 'R1': 'unit/generated_support', 'R2': 'unit/generated_support',
 # `cli/transpiler-cli` asserts the property directly (a guarded block must emit
 # `pc = n;`), which is why it is the expression. The transpiled 20-exceptions cases go
 # red too, but by HANGING -- with `pc` stuck at 0 every exception enters the same
 # handler and a handler that raises loops -- so each costs the harness's 90 s timeout
 # and proving it that way takes half an hour for no extra information.
 'R3': 'cli/transpiler-cli',
 'R4': 'unit/generated_support|transpiled/20-exceptions/nested-try',
}


def backup():
    BAK.mkdir(parents=True, exist_ok=True)
    for f in FILES:
        shutil.copy2(ROOT / f, BAK / pathlib.Path(f).name)


def restore():
    # copyfile + an explicit touch, NOT copy2: copy2 preserves the BACKUP's mtime,
    # which is older than the object file `make` already produced, so a restored
    # file is not rebuilt and the previous mutation's object survives into the next
    # measurement. That contaminated the first run of this matrix; every number
    # after the first was the union of two mutations.
    for f in FILES:
        shutil.copyfile(BAK / pathlib.Path(f).name, ROOT / f)
        (ROOT / f).touch()


if sys.argv[1] == 'covers':
    print(COVERS[sys.argv[2]])
elif sys.argv[1] == 'backup':
    backup(); print('backed up')
elif sys.argv[1] == 'restore':
    restore(); print('restored')
else:
    restore()
    f, old, new = MUTS[sys.argv[1]]
    p = ROOT / f
    s = p.read_text()
    if s.count(old) != 1:
        raise SystemExit(f'{sys.argv[1]}: anchor found {s.count(old)} times in {f}')
    p.write_text(s.replace(old, new))
    p.touch()
    print('applied', sys.argv[1])
