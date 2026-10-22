// Errors.h — Scala-level runtime errors raised by the VM and the primitives.
#pragma once
#include "protoCore.h"

#include <stdexcept>
#include <string>

namespace protoScala {

// A Scala exception raised by a native throw site of protoScala itself:
// `className` is the unqualified Scala/Java class name (ArithmeticException,
// NullPointerException, ...; D8: no java.lang prefix), `message` its message.
// `line` is the innermost source line, filled by the VM frame the error passes
// through first (0: unknown).
//
// A ScalaError is protoScala's INTERNAL, lazy form of a Scala exception: the VM
// materialises it into an instance of the prelude class only once a handler is
// known to exist (ExecutionEngine::runFrame), so the uncaught path allocates
// nothing. It is a C++ exception like every other, and it is never seen outside
// protoScala. The boundary rule (DESIGN §7): every entry a caller outside
// protoScala can make -- a UMD provider's tryLoad (ScalaModuleProvider,
// CompiledModuleProvider), a transpiled module's initialiser, a transpiled
// function's method cell called from outside the VM -- goes through leaveProtoScala (ForeignBoundary.h),
// which materialises it into a ScalaThrow first (raiseAtBoundary,
// ErrorFactory.h). The top levels (Session, a thread body, an actor turn, a
// future continuation) report it and let nothing leave.
class ScalaError : public std::runtime_error {
public:
    ScalaError(std::string className, std::string message)
        : std::runtime_error(className + ": " + message),
          className_(std::move(className)), message_(std::move(message)) {}
    const std::string& className() const { return className_; }
    const std::string& message() const { return message_; }
    int line = 0;
private:
    std::string className_;
    std::string message_;
};

// ScalaThrow — an in-flight Scala exception value, raised by the THROW and
// RETHROW opcodes and by `await` on a failed future (DESIGN §7).
//
// It is protoCore's one exception type, proto::ProtoException (protoCore 3.0.0,
// docs/EXCEPTIONS.md), with the source line added. protoCore pins the value for
// as long as any copy of the exception is in flight, so no VM frame has to
// re-root it while the stack unwinds.
//
// Every handler catches `const proto::ProtoException&`, never only this
// subclass: an error raised by protoCore (through the error factory,
// ErrorFactory.h), by a C++ UMD module or by another runtime arrives as a
// ProtoException that is not a ScalaThrow, and it is selected the same way, by
// the value's prototype chain. ProtoException does not derive from
// std::exception, so the `catch (const std::exception&)` arms written for
// library errors (runLoop, ForeignBoundary.h) can never swallow it;
// tests/unit/test_exceptions.cpp pins that.
class ScalaThrow : public proto::ProtoException {
public:
    ScalaThrow(proto::ProtoContext* ctx, const proto::ProtoObject* v)
        : proto::ProtoException(ctx, v) {}
    // The innermost source line, filled by the first VM frame the throw passes
    // through, so an uncaught report keeps D14's `file:line: error: ...` shape.
    int line = 0;
};

// The source line a ProtoException carries: a ScalaThrow's, or 0 for one raised
// outside protoScala that no VM frame has seen yet.
inline int thrownLine(const proto::ProtoException& e) {
    const auto* t = dynamic_cast<const ScalaThrow*>(&e);
    return t ? t->line : 0;
}

} // namespace protoScala
