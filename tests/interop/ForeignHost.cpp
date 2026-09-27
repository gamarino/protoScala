/*
 * The host half of the cross-runtime call demonstration. It may use protoScala freely,
 * because it IS the host; keeping it in its own translation unit is what lets the
 * caller's translation unit be checked for protoScala independence.
 */
#include "ForeignHost.h"

#include "repl/Session.h"
#include "runtime/StackGuard.h"

namespace {

struct Job {
    const char* soPath;
    ForeignBody fn;
    void* ud;
};

int body(void* arg) {
    const Job& j = *static_cast<Job*>(arg);
    protoScala::Session session;
    return session.withModule(j.soPath, j.fn, j.ud);
}

}  // namespace

int foreignHostWithModule(const char* soPath, ForeignBody fn, void* ud) {
    protoScala::configureThreadStacks();
    Job j{soPath, fn, ud};
    // The evaluator thread, for the same reason protoscala itself uses one: a Session
    // must be constructed on the thread that will run it (StackGuard.h).
    return protoScala::runOnEvaluatorThread(&body, &j);
}
