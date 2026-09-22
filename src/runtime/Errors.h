// Errors.h — Scala-level runtime errors raised by the VM and the primitives.
#pragma once
#include <stdexcept>
#include <string>

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

} // namespace protoScala
