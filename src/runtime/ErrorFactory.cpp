// ErrorFactory.cpp — see ErrorFactory.h.
#include "runtime/ErrorFactory.h"

#include "runtime/ExecutionEngine.h"
#include "runtime/Runtime.h"
#include "runtime/Values.h"

#include <mutex>
#include <string>
#include <vector>

namespace protoScala {

namespace {

struct SpaceBinding {
    const proto::ProtoSpace* space;
    ExecutionEngine* engine;
    const RuntimeLayout* layout;
};

// Intentionally leaked, like ScalaModuleProvider's host registry: a session can
// be destroyed during static destruction, after a function-local static is gone.
std::mutex* g_bindingsMu = new std::mutex;
std::vector<SpaceBinding>* g_bindings = new std::vector<SpaceBinding>;

bool boundFor(const proto::ProtoSpace* space, ExecutionEngine** engine,
              const RuntimeLayout** layout) {
    std::lock_guard<std::mutex> lock(*g_bindingsMu);
    for (const SpaceBinding& b : *g_bindings)
        if (b.space == space) {
            *engine = b.engine;
            *layout = b.layout;
            return true;
        }
    return false;
}

const proto::ProtoString* sym(proto::ProtoContext* ctx, const char* name) {
    return proto::ProtoString::createSymbol(ctx, name);
}

// The value raised when no Scala instance can be built: protoCore's own shape
// (the default factory answers a child of the kind's prototype with a `message`
// attribute), plus the Scala class name the error would have had. It allocates
// only plain objects and strings, and runs no Scala code.
const proto::ProtoObject* plainError(proto::ProtoContext* ctx, const proto::ProtoObject* proto,
                                     const std::string& className, const std::string& message) {
    const proto::ProtoObject* v = proto->newChild(ctx);
    v = v->setAttribute(ctx, sym(ctx, "name"), ctx->fromUTF8String(className.c_str()));
    return v->setAttribute(ctx, sym(ctx, "message"), ctx->fromUTF8String(message.c_str()));
}

// Builds the prelude instance `e` names with the engine that owns the space, or
// answers nullptr when there is none or building it fails. Never throws.
const proto::ProtoObject* buildScala(proto::ProtoContext* ctx, const ScalaError& e) noexcept {
    try {
        ExecutionEngine* engine = nullptr;
        const RuntimeLayout* layout = nullptr;
        const ActiveCallContext* active = activeCallContext();
        if (active && active->engine && active->layout) {
            engine = active->engine;
            layout = active->layout;
        } else if (!boundFor(ctx->space, &engine, &layout)) {
            return nullptr;
        }
        if (!layout->hooks.bound) return nullptr;   // the prelude is still loading
        if (active) return engine->materialise(ctx, e);
        // Raised on a thread that is not running Scala code (a foreign caller):
        // the constructor runs through the VM, whose natives read the active
        // call context, so install the space's own for its duration.
        ExecutionEngine::ActiveCallGuard guard(engine, layout);
        return engine->materialise(ctx, e);
    } catch (...) {
        return nullptr;
    }
}

}  // namespace

void installErrorFactory(proto::ProtoSpace* space, ExecutionEngine* engine,
                         const RuntimeLayout* layout) {
    {
        std::lock_guard<std::mutex> lock(*g_bindingsMu);
        bool replaced = false;
        for (SpaceBinding& b : *g_bindings)
            if (b.space == space) {
                b.engine = engine;
                b.layout = layout;
                replaced = true;
            }
        if (!replaced) g_bindings->push_back(SpaceBinding{space, engine, layout});
    }
    space->setErrorFactory(&scalaErrorFactory);
}

void uninstallErrorFactory(proto::ProtoSpace* space, ExecutionEngine* engine) {
    std::lock_guard<std::mutex> lock(*g_bindingsMu);
    for (auto it = g_bindings->begin(); it != g_bindings->end(); ++it)
        if (it->space == space && it->engine == engine) {
            g_bindings->erase(it);
            space->setErrorFactory(nullptr);
            return;
        }
}

const char* scalaClassForErrorKind(proto::ProtoErrorKind kind) {
    switch (kind) {
        // Integer.parseInt("ten") throws NumberFormatException on the JVM, and a
        // radix outside 2..36 is a NumberFormatException there too. It is an
        // IllegalArgumentException, so `case e: IllegalArgumentException` sees it.
        case proto::ProtoErrorKind::InvalidArgument: return "NumberFormatException";
        // An operand of the wrong type: the JVM's failed cast.
        case proto::ProtoErrorKind::TypeMismatch:    return "ClassCastException";
        // Math.toIntExact, BigInteger.longValueExact: ArithmeticException.
        case proto::ProtoErrorKind::Overflow:        return "ArithmeticException";
        // `1 / 0` on Int and Long.
        case proto::ProtoErrorKind::DivisionByZero:  return "ArithmeticException";
        // A call with too many arguments: a bad argument list.
        case proto::ProtoErrorKind::ArityMismatch:   return "IllegalArgumentException";
    }
    return "RuntimeException";
}

const proto::ProtoObject* scalaErrorFactory(proto::ProtoContext* ctx, proto::ProtoErrorKind kind,
                                            const char* message) {
    const char* cls = scalaClassForErrorKind(kind);
    const std::string msg = message ? message : "";
    // ScalaError is only the carrier of (class, message) here; it is never thrown.
    if (const proto::ProtoObject* v = buildScala(ctx, ScalaError(cls, msg))) return v;
    try {
        return plainError(ctx, ctx->space->getErrorPrototype(ctx, kind), cls, msg);
    } catch (...) {
        // The factory must return a value and must not throw. A space that cannot
        // allocate a small object is out of memory, and PROTO_NONE still unwinds.
        return PROTO_NONE;
    }
}

void raiseAtBoundary(proto::ProtoContext* ctx, const ScalaError& e) {
    const proto::ProtoObject* v = buildScala(ctx, e);
    if (!v)
        v = plainError(ctx, ctx->space->getErrorRootPrototype(ctx), e.className(), e.message());
    ScalaThrow t(ctx, v);
    t.line = e.line;
    throw t;
}

std::string describeThrown(proto::ProtoContext* ctx, const proto::ProtoObject* v) noexcept {
    try {
        const ActiveCallContext* active = activeCallContext();
        const RuntimeLayout* L = active ? active->layout : nullptr;
        const bool foreign = L && v && v != PROTO_NONE && isObjectCellFast(v) &&
                             v->isInstanceOf(ctx, L->anyProto) != PROTO_TRUE &&
                             !v->isMethod(ctx);
        if (foreign) {
            auto text = [&](const char* name) -> std::string {
                const proto::ProtoObject* a = v->getAttribute(ctx, sym(ctx, name));
                return proto::ProtoObject::isStringTagFast(a)
                           ? reinterpret_cast<const proto::ProtoString*>(a)->toStdString(ctx)
                           : std::string();
            };
            const std::string name = text("name");
            const std::string message = text("message");
            if (!name.empty() || !message.empty()) {
                const std::string shown = name.empty() ? "<foreign error>" : name;
                return message.empty() ? shown : shown + ": " + message;
            }
        }
        if (!active || !active->engine) return "<exception>";
        return active->engine->showTopLevel(ctx, v);
    } catch (...) {
        return "<exception whose toString failed>";
    }
}

}  // namespace protoScala
