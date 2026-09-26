#!/usr/bin/env python3
"""Apply one named mutation of the transpiler, or restore. Phase 7 Task 12."""
import pathlib, sys, shutil

ROOT = pathlib.Path('/home/gamarino/Documentos/proyectos/protoScala')
BAK = pathlib.Path('/home/gamarino/Documentos/proyectos/.agent_scratch/phase7-transpiler/mutbak')
FILES = ['src/compiler/CppEmitter.cpp', 'src/runtime/GeneratedSupport.cpp',
         'src/umd/ForeignBoundary.h', 'src/compiler/CppTables.cpp']

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


if sys.argv[1] == 'backup':
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
