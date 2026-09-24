// Errors.h — Scala-level runtime errors raised by the VM and the primitives.
#pragma once
#include <stdexcept>
#include <string>

namespace proto { class ProtoObject; }

namespace protoScala {

// A Scala exception: `className` is the unqualified Scala/Java class name
// (ArithmeticException, NullPointerException, ...; D8: no java.lang prefix),
// `message` its message. `line` is the innermost source line, filled by the
// VM frame the error passes through first (0: unknown).
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
// It derives from std::exception and deliberately NOT from std::runtime_error:
// ExecutionEngine::runLoop catches `const std::runtime_error&` and turns it
// into a RuntimeException (the protoCore-error bridge), which would swallow
// every Scala throw if ScalaThrow were a runtime_error, and every
// user-defined exception class would stop being catchable by its own name.
// tests/unit/test_exceptions.cpp pins that with a static_assert and a
// throw/catch test, because the failure would be silent and total.
//
// `value` is re-rooted by every frame the exception passes through (plan
// A0-2, escalation E1): the frame writes it into its own
// ProtoContext::returnValue, a traced slot the exception path never otherwise
// uses, before anything allocates. The only window in which it is unrooted is
// between one frame's context being destroyed and the next frame's catch
// running, and nothing in that window allocates: C++ unwinding runs
// destructors, and ProtoContext::~ProtoContext does not allocate.
class ScalaThrow : public std::exception {
public:
    explicit ScalaThrow(const proto::ProtoObject* v) : value(v) {}
    const char* what() const noexcept override { return "protoScala exception"; }
    const proto::ProtoObject* value;
    // The innermost source line, filled by the first VM frame the throw passes
    // through, so an uncaught report keeps D14's `file:line: error: ...` shape.
    int line = 0;
};

} // namespace protoScala
