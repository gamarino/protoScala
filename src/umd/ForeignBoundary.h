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
 * tests/unit/test_exceptions.cpp has one case per clause. Removing any clause
 * turns one of them red.
 */
#pragma once
#include "runtime/Errors.h"
#include "runtime/FutureYield.h"

#include <stdexcept>
#include <utility>

namespace protoScala {

template <typename Call>
auto translateForeignException(Call&& call) -> decltype(call()) {
    try {
        return call();
    }
    catch (FutureYield&)            { throw; }   // a cooperative suspension, not an error
    catch (ScalaThrow&)             { throw; }   // a Scala exception already in flight
    catch (ScalaError&)             { throw; }   // a native throw site's own translation
    catch (const std::logic_error&) { throw; }   // D74: a VM defect stays uncatchable
    catch (const std::exception& e) { throw ScalaError("RuntimeException", e.what()); }
    catch (...)                     { throw ScalaError("RuntimeException", "native exception"); }
}

} // namespace protoScala
