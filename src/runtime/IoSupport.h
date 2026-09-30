/*
 * IoSupport — what the input/output primitive files share: the bracket every
 * blocking call runs in, the mapping of protoIO's error kinds onto the prelude's
 * exception classes, English errno reasons, UTF-8 checking and decoding, and the
 * `Bytes` value's representation.
 *
 * The bracket (DESIGN §3, P6). protoIO's functions may block, on a peer, a child
 * process, a terminal or a pipe. A thread blocked in the kernel reaches no
 * safepoint, so every such call runs inside `ProtoContext::UnmanagedScope`: the
 * thread leaves the collector's quorum for the duration and a collection never
 * waits for it. The rule that makes this sound is that NOTHING inside the bracket
 * touches a ProtoObject — arguments are copied into C++ values before it opens,
 * and results are built into ProtoObjects after it closes. `blocking()` also
 * tells the actor scheduler that a worker is about to block, so the pool can grow
 * rather than starve (ActorScheduler::enterBlocking).
 */
#pragma once
#include "runtime/Errors.h"
#include "protoCore.h"
#include "protoio/error.h"

#include <string>
#include <string_view>
#include <utility>

namespace protoScala::io {

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

// The English text of an errno, for every errno a file or socket operation can
// report; an errno outside the table falls back to std::strerror, which is
// localised (D98).
const char* reasonFor(int e);

// `<path> (<reason>)`, the JVM's message shape (D98).
std::string located(const std::string& path, int e);

// The prelude class a protoIO error kind is raised as (D124).
const char* classFor(const protoio::Error& e);

// protoIO's message with its trailing strerror text (localised) replaced by the
// English reason, so a message does not depend on the locale.
std::string englishMessage(const protoio::Error& e);

// Raises `e` as the prelude exception its kind maps to.
[[noreturn]] void raise(const protoio::Error& e);

// ---------------------------------------------------------------------------
// The bracket
// ---------------------------------------------------------------------------

// Tells the actor scheduler that the calling thread is about to block in I/O,
// for the lifetime of the object. Constructed BEFORE the UnmanagedScope opens,
// because growing the pool creates a thread, which allocates.
class BlockingAccount {
public:
    explicit BlockingAccount(proto::ProtoContext* ctx);
    ~BlockingAccount();
    BlockingAccount(const BlockingAccount&) = delete;
    BlockingAccount& operator=(const BlockingAccount&) = delete;
private:
    bool active_;
};

// For a call that can wait without bound: on a peer, a child process, a
// terminal or a pipe. `f` must touch no ProtoObject.
template <typename F>
auto blocking(proto::ProtoContext* ctx, F&& f) -> decltype(f()) {
    try {
        BlockingAccount accounted(ctx);
        proto::ProtoContext::UnmanagedScope out(ctx);
        return f();
    } catch (const protoio::Error& e) {
        raise(e);
    }
}

// For a call that blocks only briefly (the local file system, binding a
// socket): out of the collector's quorum, without growing the worker pool.
template <typename F>
auto unmanaged(proto::ProtoContext* ctx, F&& f) -> decltype(f()) {
    try {
        proto::ProtoContext::UnmanagedScope out(ctx);
        return f();
    } catch (const protoio::Error& e) {
        raise(e);
    }
}

// For a protoIO call that does not block.
template <typename F>
auto immediate(F&& f) -> decltype(f()) {
    try {
        return f();
    } catch (const protoio::Error& e) {
        raise(e);
    }
}

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

// The byte offset of the first malformed byte, or -1 when `s` is well-formed
// UTF-8. Strict, as the JVM's decoder is: overlong forms, surrogates, code
// points above U+10FFFF and a sequence truncated at the end are malformed.
long long firstMalformedUTF8(std::string_view s);

// `s` with every malformed sequence replaced by U+FFFD, as java.io's readers
// decode a stream (the REPLACE action). Well-formed input is answered as is.
std::string lenientUTF8(std::string_view s);

// ---------------------------------------------------------------------------
// Bytes
// ---------------------------------------------------------------------------

// Binary data crosses into protoScala as a protoCore ProtoByteBuffer (P1, P3:
// the kernel already has the type whose semantic this is), which the prelude's
// `Bytes` class wraps. These convert between it and a C++ string of octets.
const proto::ProtoObject* makeByteBuffer(proto::ProtoContext* ctx, std::string_view octets);
// The octets of a ProtoByteBuffer; throws ClassCastException naming `method`
// when `v` is not one.
std::string byteBufferArg(proto::ProtoContext* ctx, const proto::ProtoObject* v, const char* method);

} // namespace protoScala::io
