/*
 * translateForeignException — the mandatory boundary shape (ROADMAP Phase 6,
 * protoST's exceptions spec §6). Every call that leaves protoScala for a UMD
 * provider or a foreign callable goes through it.
 *
 * THE CATCH ORDER IS LOAD-BEARING, clause by clause:
 *
 *  - FutureYield is NOT a std::exception and must be first anyway: a later
 *    catch(...) would eat a cooperative suspension and the actor would never
 *    resume.
 *  - ScalaThrow IS a std::exception (and deliberately not a std::runtime_error).
 *    Without this clause the std::exception arm would rewrite every Scala
 *    exception crossing a module boundary into a RuntimeException, losing its
 *    class and its payload.
 *  - ScalaError is already a translation; re-translating it would replace a
 *    precise class name with RuntimeException.
 *  - std::logic_error must NOT be translated and must NOT be catchable (D74):
 *    a compiler or VM defect must never be maskable by `catch { case e:
 *    Throwable => }`. It is re-thrown BEFORE the std::exception arm, which
 *    would otherwise catch it — std::logic_error derives from std::exception.
 *    ROADMAP's prescribed five clauses do not mention it, and without it this
 *    template would retire D74 silently.
 *  - std::exception carries what() across.
 *  - catch(...) is the last resort and says so rather than inventing a message.
 *
 * Every clause captures its exception, translated or not, and the template
 * throws it AFTER the catch clause has completed (std::rethrow_exception), as
 * the VM's frames do. Under the Itanium ABI that is the same as `throw;` inside
 * the clause; under MSVC a catch clause runs before the stack below it is
 * released, so a re-throw inside it keeps that stack, and a StackOverflowError
 * leaving a deep recursion of transpiled functions (each of which is entered
 * through this boundary) overflowed the native stack again while it propagated.
 *
 * tests/unit/test_exceptions.cpp has one case per clause. Removing any clause
 * turns one of them red.
 */
#pragma once
#include "runtime/Errors.h"
#include "runtime/FutureYield.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace protoScala {

template <typename Call>
auto translateForeignException(Call&& call) -> decltype(call()) {
    std::exception_ptr passOn;
    try {
        return call();
    }
    catch (FutureYield&)            { passOn = std::current_exception(); }  // a cooperative suspension, not an error
    catch (ScalaThrow&)             { passOn = std::current_exception(); }  // a Scala exception already in flight
    catch (ScalaError&)             { passOn = std::current_exception(); }  // a native throw site's own translation
    catch (const std::logic_error&) { passOn = std::current_exception(); }  // D74: a VM defect stays uncatchable
    catch (const std::exception& e) {
        passOn = std::make_exception_ptr(ScalaError("RuntimeException", e.what()));
    }
    catch (...) {
        passOn = std::make_exception_ptr(ScalaError("RuntimeException", "native exception"));
    }
    std::rethrow_exception(passOn);
}

} // namespace protoScala
