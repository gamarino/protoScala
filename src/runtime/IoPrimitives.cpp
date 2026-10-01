/*
 * IoPrimitives — the natives behind the prelude's input/output surface that is
 * not about files (FilePrimitives.cpp) or HTTP (HttpPrimitives.cpp): `Bytes`,
 * the running program (`sys.env`, `sys.exit`, `sys.props`, standard error),
 * other programs (`Process`) and sockets (`Socket`, `ServerSocket`,
 * `DatagramSocket`). Every one is a hidden `__`-prefixed global named by
 * builtinGlobalNames(); the public surface is written in lib/prelude.scala on
 * top of them, as Track F's is.
 *
 * All of them are bindings over protoIO: arguments are copied into C++ values,
 * the call runs inside the bracket of IoSupport.h, and the result is built into
 * ProtoObjects after the bracket closes. Descriptors cross into protoScala as
 * plain Ints, held by the prelude's socket classes.
 */
#include "runtime/ActorScheduler.h"
#include "runtime/IoSupport.h"
#include "runtime/Primitives.h"
#include "runtime/PrimitiveSupport.h"

#include "protoio/file.h"
#include "protoio/net.h"
#include "protoio/process.h"
#include "protoio/stream.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

namespace protoScala {

namespace io {

// ---------------------------------------------------------------------------
// Errors (D124)
// ---------------------------------------------------------------------------

const char* classFor(const protoio::Error& e) {
    using K = protoio::Error::Kind;
    switch (e.kind) {
        case K::FileNotFound:       return "FileNotFoundException";
        case K::FileExists:         return "FileAlreadyExistsException";
        case K::FileSystem:         return "IOException";
        case K::ConnectionRefused:  return "ConnectException";
        case K::ConnectionTimedOut: return "SocketTimeoutException";
        case K::NameLookup:         return "UnknownHostException";
        case K::Network:            return "SocketException";
        // scala.sys.process reports a program it cannot run as an IOException
        // ("Cannot run program ...").
        case K::Process:            return "IOException";
        case K::LineTooLong:        return "IOException";
        case K::BodyTooLarge:       return "IOException";
        case K::InvalidArgument:    return "IllegalArgumentException";
    }
    return "IOException";
}

std::string englishMessage(const protoio::Error& e) {
    std::string m = e.what();
    if (e.sysErrno != 0) {
        const std::string local = std::strerror(e.sysErrno);
        if (m.size() >= local.size() && m.compare(m.size() - local.size(), local.size(), local) == 0)
            m = m.substr(0, m.size() - local.size()) + reasonFor(e.sysErrno);
    }
    return m;
}

void raise(const protoio::Error& e) { throw ScalaError(classFor(e), englishMessage(e)); }

// ---------------------------------------------------------------------------
// The bracket's pool accounting
// ---------------------------------------------------------------------------

BlockingAccount::BlockingAccount(proto::ProtoContext* ctx)
    : active_(ActorScheduler::instance().enterBlocking(ctx)) {}

BlockingAccount::~BlockingAccount() {
    if (active_) ActorScheduler::instance().leaveBlocking();
}

// ---------------------------------------------------------------------------
// Bytes
// ---------------------------------------------------------------------------

const proto::ProtoObject* makeByteBuffer(proto::ProtoContext* ctx, std::string_view octets) {
    return ctx->newByteBuffer(octets.data(), octets.size())->asObject(ctx);
}

std::string byteBufferArg(proto::ProtoContext* ctx, const proto::ProtoObject* v, const char* method) {
    if (!v || v == PROTO_NONE || !v->isByteBuffer(ctx))
        prim::wrongType(ctx, method, "Bytes", v);
    const proto::ProtoByteBuffer* b = v->asByteBuffer(ctx);
    const proto::proto_ulong n = b->getSize(ctx);
    return n == 0 ? std::string() : std::string(b->getBuffer(ctx), n);
}

} // namespace io

namespace prim {
namespace {

// A String, or the ProtoByteBuffer a Bytes wraps, as octets: what may be
// written to a socket, a child's input or an HTTP body.
std::string octetsArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (proto::ProtoObject::isStringTagFast(v)) return asStr(v)->toStdString(ctx);
    return io::byteBufferArg(ctx, v, method);
}

std::vector<std::string> stringSeqArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    const SeqView sv = seqViewOf(ctx, layoutOf(), v);
    if (!sv.valid) wrongType(ctx, method, "a Seq[String]", v);
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(sv.size));
    for (long long i = 0; i < sv.size; ++i)
        out.push_back(stringArg(ctx, seqElemAt(ctx, sv, i), method));
    return out;
}

int portArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    const long long p = intArg(ctx, v, method);
    if (p < 0 || p > 65535)
        throw ScalaError("IllegalArgumentException",
                         std::string(method) + ": port out of range: " + std::to_string(p));
    return static_cast<int>(p);
}

int fdArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    return static_cast<int>(intArg(ctx, v, method));
}

// Text that arrives from a peer, a child or a terminal is decoded as java.io's
// readers decode it: a malformed sequence becomes U+FFFD (D125).
const ProtoObject* text(ProtoContext* ctx, std::string_view bytes) {
    return makeString(ctx, io::lenientUTF8(bytes));
}

// ---------------------------------------------------------------------------
// Bytes (D126)
// ---------------------------------------------------------------------------

std::string bufArg(ProtoContext* ctx, const ProtoList* args, proto::proto_ulong i, const char* method) {
    return io::byteBufferArg(ctx, args->getAt(ctx, static_cast<int>(i)), method);
}

// __bytesFromSeq(seq) -> buffer: each element an Int in -128..255.
PRIM(bytes_from_seq) {
    const ProtoObject* v = arg(ctx, args, 0, "Bytes", 1);
    const SeqView sv = seqViewOf(ctx, layoutOf(), v);
    if (!sv.valid) wrongType(ctx, "Bytes", "a Seq[Int]", v);
    std::string out;
    out.reserve(static_cast<std::size_t>(sv.size));
    for (long long i = 0; i < sv.size; ++i) {
        const long long b = intArg(ctx, seqElemAt(ctx, sv, i), "Bytes");
        if (b < -128 || b > 255)
            throw ScalaError("IllegalArgumentException",
                             "Bytes: " + std::to_string(b) + " is not a byte (-128..255)");
        out.push_back(static_cast<char>(static_cast<unsigned char>(b & 0xFF)));
    }
    return io::makeByteBuffer(ctx, out);
}

// __bytesFromString(s) -> buffer: the UTF-8 encoding (String.getBytes).
PRIM(bytes_from_string) {
    const std::string s = stringArg(ctx, arg(ctx, args, 0, "getBytes", 1), "getBytes");
    return io::makeByteBuffer(ctx, s);
}

PRIM(bytes_length) {
    expectArgs(ctx, args, "Bytes.length", 1);
    const ProtoObject* v = args->getAt(ctx, 0);
    if (!v || v == PROTO_NONE || !v->isByteBuffer(ctx)) wrongType(ctx, "Bytes.length", "Bytes", v);
    return ctx->fromInteger(static_cast<long long>(v->asByteBuffer(ctx)->getSize(ctx)));
}

// __bytesAt(buffer, i) -> Int, Scala's signed Byte value.
PRIM(bytes_at) {
    expectArgs(ctx, args, "Bytes.apply", 2);
    const std::string b = bufArg(ctx, args, 0, "Bytes.apply");
    const long long i = intArg(ctx, args->getAt(ctx, 1), "Bytes.apply");
    if (i < 0 || i >= static_cast<long long>(b.size()))
        throw ScalaError("IndexOutOfBoundsException", "Index " + std::to_string(i) +
                                                          " out of bounds for length " +
                                                          std::to_string(b.size()));
    return ctx->fromInteger(static_cast<long long>(static_cast<signed char>(b[static_cast<std::size_t>(i)])));
}

// __bytesSlice(buffer, from, until) -> buffer, clamped as Scala's slice is.
PRIM(bytes_slice) {
    expectArgs(ctx, args, "Bytes.slice", 3);
    const std::string b = bufArg(ctx, args, 0, "Bytes.slice");
    const long long n = static_cast<long long>(b.size());
    long long from = intArg(ctx, args->getAt(ctx, 1), "Bytes.slice");
    long long until = intArg(ctx, args->getAt(ctx, 2), "Bytes.slice");
    from = from < 0 ? 0 : (from > n ? n : from);
    until = until < from ? from : (until > n ? n : until);
    return io::makeByteBuffer(ctx, std::string_view(b).substr(static_cast<std::size_t>(from),
                                                             static_cast<std::size_t>(until - from)));
}

PRIM(bytes_concat) {
    expectArgs(ctx, args, "Bytes.++", 2);
    return io::makeByteBuffer(ctx, bufArg(ctx, args, 0, "Bytes.++") + bufArg(ctx, args, 1, "Bytes.++"));
}

PRIM(bytes_equals) {
    expectArgs(ctx, args, "Bytes.equals", 2);
    return boolean(bufArg(ctx, args, 0, "Bytes.equals") == bufArg(ctx, args, 1, "Bytes.equals"));
}

// java.util.Arrays.hashCode(byte[]): 31 * h + b over the signed values.
PRIM(bytes_hash) {
    const std::string b = io::byteBufferArg(ctx, arg(ctx, args, 0, "Bytes.hashCode", 1), "Bytes.hashCode");
    std::int32_t h = 1;
    for (const char c : b)
        h = static_cast<std::int32_t>(31u * static_cast<std::uint32_t>(h) +
                                      static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<signed char>(c))));
    return ctx->fromInteger(h);
}

// __bytesToList(buffer) -> List[Int] of the signed values.
PRIM(bytes_to_list) {
    const std::string b = io::byteBufferArg(ctx, arg(ctx, args, 0, "Bytes.toList", 1), "Bytes.toList");
    std::vector<const ProtoObject*> items;
    items.reserve(b.size());
    // SmallIntegers are immediate values, not cells: holding them in a C++
    // vector across the list's allocation is safe (P1 concerns cells only).
    for (const char c : b) items.push_back(ctx->fromInteger(static_cast<signed char>(c)));
    return ctx->newList(static_cast<unsigned>(items.size()), items.data())->asObject(ctx);
}

// __bytesDecode(buffer) -> String, as `new String(bytes, UTF_8)` decodes.
PRIM(bytes_decode) {
    const std::string b = io::byteBufferArg(ctx, arg(ctx, args, 0, "Bytes.utf8String", 1), "Bytes.utf8String");
    return text(ctx, b);
}

// __bytesShow(buffer) -> "Bytes(0, -1, 10)"
PRIM(bytes_show) {
    const std::string b = io::byteBufferArg(ctx, arg(ctx, args, 0, "Bytes.toString", 1), "Bytes.toString");
    std::string out = "Bytes(";
    for (std::size_t i = 0; i < b.size(); ++i) {
        if (i) out += ", ";
        out += std::to_string(static_cast<int>(static_cast<signed char>(b[i])));
    }
    out += ")";
    return makeString(ctx, out);
}

// ---------------------------------------------------------------------------
// The running program
// ---------------------------------------------------------------------------

// __ioEnv() -> List(name, value, name, value, ...)
PRIM(io_env) {
    expectArgs(ctx, args, "sys.env", 0);
    const std::vector<std::pair<std::string, std::string>> env = protoio::process::environment();
    ListBuilder out(ctx);
    for (const auto& [k, v] : env) {
        out.add(text(out.context(), k));
        out.add(text(out.context(), v));
    }
    return out.finish();
}

// __ioProps() -> List(name, value, ...): the JVM's standard system properties
// that have a meaning here (D127).
PRIM(io_props) {
    expectArgs(ctx, args, "sys.props", 0);
    std::vector<std::pair<std::string, std::string>> props;
    const std::pair<std::string, std::string> dirs = io::unmanaged(ctx, [] {
        std::string cwd;
        try { cwd = protoio::file::cwd(); } catch (const protoio::Error&) {}
        return std::make_pair(cwd, protoio::file::tempDir());
    });
    props.emplace_back("user.dir", dirs.first);
#if defined(_WIN32)
    if (const auto home = protoio::process::getenv("USERPROFILE")) props.emplace_back("user.home", *home);
    if (const auto user = protoio::process::getenv("USERNAME")) props.emplace_back("user.name", *user);
    props.emplace_back("os.name", "Windows");
    SYSTEM_INFO si {};
    ::GetNativeSystemInfo(&si);
    props.emplace_back("os.arch", si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64 ? "amd64"
                                : si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64 ? "aarch64"
                                                                                            : "x86");
    // GetVersionEx reports the version the manifest claims; ntdll reports the real one.
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    if (const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
            ::GetProcAddress(::GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))) {
        OSVERSIONINFOW v {};
        v.dwOSVersionInfoSize = sizeof v;
        if (rtlGetVersion(&v) == 0)
            props.emplace_back("os.version", std::to_string(v.dwMajorVersion) + "." +
                                             std::to_string(v.dwMinorVersion));
    }
    // println writes "\n" and the standard streams are binary (src/main.cpp), and
    // Windows accepts '/' in every path, so a program sees the same line and file
    // separators everywhere. A list of paths (PATH, PROTOSCALA_MODULE_PATH) is
    // separated by ';' here, as drive letters contain ':'.
    props.emplace_back("line.separator", "\n");
    props.emplace_back("file.separator", "/");
    props.emplace_back("path.separator", ";");
#else
    if (const auto home = protoio::process::getenv("HOME")) props.emplace_back("user.home", *home);
    if (const auto user = protoio::process::getenv("USER")) props.emplace_back("user.name", *user);
    struct utsname u {};
    if (::uname(&u) == 0) {
        props.emplace_back("os.name", u.sysname);
        props.emplace_back("os.arch", u.machine);
        props.emplace_back("os.version", u.release);
    }
    props.emplace_back("line.separator", "\n");
    props.emplace_back("file.separator", "/");
    props.emplace_back("path.separator", ":");
#endif
    props.emplace_back("java.io.tmpdir", dirs.second);
    ListBuilder out(ctx);
    for (const auto& [k, v] : props) {
        out.add(text(out.context(), k));
        out.add(text(out.context(), v));
    }
    return out.finish();
}

// __ioExit(status): ends the process at once, as System.exit does: the output
// written so far is flushed, and nothing after the call runs -- no `finally`,
// no further actor messages (D128).
PRIM(io_exit) {
    const long long code = intArg(ctx, arg(ctx, args, 0, "sys.exit", 1), "sys.exit");
    std::fflush(nullptr);
    protoio::process::exit(static_cast<int>(code));
}

// __ioStderr(text): writes to standard error (Console.err / System.err).
PRIM(io_stderr) {
    const std::string s = stringArg(ctx, arg(ctx, args, 0, "System.err", 1), "System.err");
    std::fflush(stdout);
    std::fwrite(s.data(), 1, s.size(), stderr);
    std::fflush(stderr);
    return layoutOf().unit;
}

// ---------------------------------------------------------------------------
// Other programs (scala.sys.process's shape, D129)
// ---------------------------------------------------------------------------

std::optional<std::string> inputArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (!v || v == PROTO_NONE) return std::nullopt;
    return octetsArg(ctx, v, method);
}

// __procRunInherit(argv, input | null) -> Int. `!`: the child writes to this
// program's own standard output and error. Without input it shares them
// directly; with input its output is collected and written through in order.
PRIM(proc_run_inherit) {
    expectArgs(ctx, args, "Process.!", 2);
    const std::vector<std::string> argv = stringSeqArg(ctx, args->getAt(ctx, 0), "Process.!");
    const std::optional<std::string> input = inputArg(ctx, args->getAt(ctx, 1), "Process.!");
    std::fflush(stdout);
    std::fflush(stderr);
    const int code = io::blocking(ctx, [&] {
        if (!input) return protoio::process::wait(protoio::process::spawn(argv));
        const protoio::process::RunResult r = protoio::process::run(argv, input);
        std::fwrite(r.out.data(), 1, r.out.size(), stdout);
        std::fflush(stdout);
        std::fwrite(r.err.data(), 1, r.err.size(), stderr);
        std::fflush(stderr);
        return r.exitCode;
    });
    return ctx->fromInteger(code);
}

// __procCapture(argv, input | null) -> List(exitCode, stdout). `!!`: standard
// output is captured; standard error still reaches this program's.
PRIM(proc_capture) {
    expectArgs(ctx, args, "Process.!!", 2);
    const std::vector<std::string> argv = stringSeqArg(ctx, args->getAt(ctx, 0), "Process.!!");
    const std::optional<std::string> input = inputArg(ctx, args->getAt(ctx, 1), "Process.!!");
    std::fflush(stdout);
    const protoio::process::RunResult r =
        io::blocking(ctx, [&] { return protoio::process::run(argv, input); });
    if (!r.err.empty()) {
        std::fwrite(r.err.data(), 1, r.err.size(), stderr);
        std::fflush(stderr);
    }
    ListBuilder out(ctx);
    out.add(out.context()->fromInteger(r.exitCode));
    out.add(text(out.context(), r.out));
    return out.finish();
}

// __procSpawn(argv) -> pid. `run()`: the child shares the standard streams.
PRIM(proc_spawn) {
    const std::vector<std::string> argv =
        stringSeqArg(ctx, arg(ctx, args, 0, "Process.run", 1), "Process.run");
    std::fflush(stdout);
    std::fflush(stderr);
    return ctx->fromInteger(io::unmanaged(ctx, [&] { return protoio::process::spawn(argv); }));
}

// __procWait(pid) -> exit code (128 + signal for a signalled child).
PRIM(proc_wait) {
    const int pid = static_cast<int>(intArg(ctx, arg(ctx, args, 0, "exitValue", 1), "exitValue"));
    return ctx->fromInteger(io::blocking(ctx, [&] { return protoio::process::wait(pid); }));
}

// __procKill(pid, signal) -> Unit
PRIM(proc_kill) {
    expectArgs(ctx, args, "destroy", 2);
    const int pid = static_cast<int>(intArg(ctx, args->getAt(ctx, 0), "destroy"));
    const int sig = static_cast<int>(intArg(ctx, args->getAt(ctx, 1), "destroy"));
    io::immediate([&] { protoio::process::kill(pid, sig); });
    return layoutOf().unit;
}

// ---------------------------------------------------------------------------
// TCP and TLS (java.net's names, simplified streams: D130)
// ---------------------------------------------------------------------------

// __tcpConnect(host, port, timeoutMs) -> fd
PRIM(tcp_connect) {
    expectArgs(ctx, args, "Socket", 3);
    const std::string host = stringArg(ctx, args->getAt(ctx, 0), "Socket");
    const int port = portArg(ctx, args->getAt(ctx, 1), "Socket");
    const int timeout = static_cast<int>(intArg(ctx, args->getAt(ctx, 2), "Socket"));
    return ctx->fromInteger(
        io::blocking(ctx, [&] { return protoio::net::tcpConnect(host, port, timeout); }));
}

// __tcpListen(host, port, backlog) -> fd
PRIM(tcp_listen) {
    expectArgs(ctx, args, "ServerSocket", 3);
    const std::string host = stringArg(ctx, args->getAt(ctx, 0), "ServerSocket");
    const int port = portArg(ctx, args->getAt(ctx, 1), "ServerSocket");
    const int backlog = static_cast<int>(intArg(ctx, args->getAt(ctx, 2), "ServerSocket"));
    return ctx->fromInteger(
        io::unmanaged(ctx, [&] { return protoio::net::tcpListen(host, port, backlog); }));
}

// __tcpAccept(fd, timeoutMs) -> fd, or -1 when the time ran out or the
// listening socket was closed.
PRIM(tcp_accept) {
    expectArgs(ctx, args, "ServerSocket.accept", 2);
    const int fd = fdArg(ctx, args->getAt(ctx, 0), "ServerSocket.accept");
    const int timeout = static_cast<int>(intArg(ctx, args->getAt(ctx, 1), "ServerSocket.accept"));
    const std::optional<int> c =
        io::blocking(ctx, [&] { return protoio::net::tcpAccept(fd, timeout); });
    return ctx->fromInteger(c ? *c : -1);
}

const ProtoObject* addressList(ProtoContext* ctx, const protoio::net::Address& a) {
    ListBuilder out(ctx);
    out.add(text(out.context(), a.host));
    out.add(out.context()->fromInteger(a.port));
    return out.finish();
}

// __sockName(fd) / __peerName(fd) -> List(host, port)
PRIM(sock_name) {
    const int fd = fdArg(ctx, arg(ctx, args, 0, "getLocalPort", 1), "getLocalPort");
    const protoio::net::Address a = io::immediate([&] { return protoio::net::sockName(fd); });
    return addressList(ctx, a);
}

PRIM(peer_name) {
    const int fd = fdArg(ctx, arg(ctx, args, 0, "getPort", 1), "getPort");
    const protoio::net::Address a = io::immediate([&] { return protoio::net::peerName(fd); });
    return addressList(ctx, a);
}

// __fdReadLine(fd, max) -> String | null
PRIM(fd_read_line) {
    expectArgs(ctx, args, "readLine", 2);
    const int fd = fdArg(ctx, args->getAt(ctx, 0), "readLine");
    const long long max = intArg(ctx, args->getAt(ctx, 1), "readLine");
    const std::optional<std::string> line = io::blocking(ctx, [&] {
        return protoio::readLine(fd, max > 0 ? static_cast<std::size_t>(max) : 0);
    });
    if (!line) return PROTO_NONE;
    return text(ctx, *line);
}

// __fdRead(fd, n) -> String of up to n whole characters | null at the end
PRIM(fd_read_chars) {
    expectArgs(ctx, args, "read", 2);
    const int fd = fdArg(ctx, args->getAt(ctx, 0), "read");
    const long long n = intArg(ctx, args->getAt(ctx, 1), "read");
    if (n < 0) throw ScalaError("IllegalArgumentException", "read: negative count " + std::to_string(n));
    const std::optional<std::string> s =
        io::blocking(ctx, [&] { return protoio::readChars(fd, static_cast<std::size_t>(n)); });
    if (!s) return PROTO_NONE;
    return text(ctx, *s);
}

// __fdReadBytes(fd, n) -> buffer of up to n bytes | null at the end
PRIM(fd_read_bytes) {
    expectArgs(ctx, args, "readBytes", 2);
    const int fd = fdArg(ctx, args->getAt(ctx, 0), "readBytes");
    const long long n = intArg(ctx, args->getAt(ctx, 1), "readBytes");
    if (n < 0) throw ScalaError("IllegalArgumentException", "readBytes: negative count " + std::to_string(n));
    const std::optional<std::string> s =
        io::blocking(ctx, [&] { return protoio::readBytes(fd, static_cast<std::size_t>(n)); });
    if (!s) return PROTO_NONE;
    return io::makeByteBuffer(ctx, *s);
}

// __fdReadAll(fd) -> String
PRIM(fd_read_all) {
    const int fd = fdArg(ctx, arg(ctx, args, 0, "readAll", 1), "readAll");
    const std::string s = io::blocking(ctx, [&] { return protoio::readAll(fd); });
    return text(ctx, s);
}

// __fdWrite(fd, String | buffer) -> Unit
PRIM(fd_write) {
    expectArgs(ctx, args, "write", 2);
    const int fd = fdArg(ctx, args->getAt(ctx, 0), "write");
    const std::string data = octetsArg(ctx, args->getAt(ctx, 1), "write");
    io::blocking(ctx, [&] { protoio::write(fd, data); });
    return layoutOf().unit;
}

// __fdSetTimeout(fd, ms) -> Unit; -1 waits without limit.
PRIM(fd_set_timeout) {
    expectArgs(ctx, args, "setSoTimeout", 2);
    const int fd = fdArg(ctx, args->getAt(ctx, 0), "setSoTimeout");
    const int ms = static_cast<int>(intArg(ctx, args->getAt(ctx, 1), "setSoTimeout"));
    io::immediate([&] { protoio::setTimeout(fd, ms); });
    return layoutOf().unit;
}

// __fdClose(fd) -> Unit
PRIM(fd_close) {
    const int fd = fdArg(ctx, arg(ctx, args, 0, "close", 1), "close");
    io::unmanaged(ctx, [&] { protoio::close(fd); });
    return layoutOf().unit;
}

// __tlsConnect(fd, host, verify) -> Unit
PRIM(tls_connect) {
    expectArgs(ctx, args, "startTls", 3);
    const int fd = fdArg(ctx, args->getAt(ctx, 0), "startTls");
    const std::string host = stringArg(ctx, args->getAt(ctx, 1), "startTls");
    const bool verify = args->getAt(ctx, 2) != PROTO_FALSE;
    io::blocking(ctx, [&] { protoio::net::tlsConnect(fd, host, verify); });
    return layoutOf().unit;
}

// ---------------------------------------------------------------------------
// UDP
// ---------------------------------------------------------------------------

// __udpBind(host, port) -> fd
PRIM(udp_bind) {
    expectArgs(ctx, args, "DatagramSocket", 2);
    const std::string host = stringArg(ctx, args->getAt(ctx, 0), "DatagramSocket");
    const int port = portArg(ctx, args->getAt(ctx, 1), "DatagramSocket");
    return ctx->fromInteger(io::unmanaged(ctx, [&] { return protoio::net::udpBind(host, port); }));
}

// __udpSend(fd, host, port, String | buffer) -> Unit
PRIM(udp_send) {
    expectArgs(ctx, args, "DatagramSocket.send", 4);
    const int fd = fdArg(ctx, args->getAt(ctx, 0), "DatagramSocket.send");
    const std::string host = stringArg(ctx, args->getAt(ctx, 1), "DatagramSocket.send");
    const int port = portArg(ctx, args->getAt(ctx, 2), "DatagramSocket.send");
    const std::string data = octetsArg(ctx, args->getAt(ctx, 3), "DatagramSocket.send");
    io::unmanaged(ctx, [&] { protoio::net::udpSend(fd, host, port, data); });
    return layoutOf().unit;
}

// __udpReceive(fd, timeoutMs) -> List(buffer, host, port) | null when the
// time ran out or the socket was closed.
PRIM(udp_receive) {
    expectArgs(ctx, args, "DatagramSocket.receive", 2);
    const int fd = fdArg(ctx, args->getAt(ctx, 0), "DatagramSocket.receive");
    const int timeout = static_cast<int>(intArg(ctx, args->getAt(ctx, 1), "DatagramSocket.receive"));
    const std::optional<protoio::net::Datagram> d =
        io::blocking(ctx, [&] { return protoio::net::udpReceive(fd, timeout); });
    if (!d) return PROTO_NONE;
    ListBuilder out(ctx);
    out.add(io::makeByteBuffer(out.context(), d->data));
    out.add(text(out.context(), d->host));
    out.add(out.context()->fromInteger(d->port));
    return out.finish();
}

#undef PRIM

}  // namespace
}  // namespace prim

void installIoPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& L) {
    using namespace protoScala::prim;
    static constexpr MethodEntry globals[] = {
        {"__bytesFromSeq", &bytes_from_seq},
        {"__bytesFromString", &bytes_from_string},
        {"__bytesLength", &bytes_length},
        {"__bytesAt", &bytes_at},
        {"__bytesSlice", &bytes_slice},
        {"__bytesConcat", &bytes_concat},
        {"__bytesEquals", &bytes_equals},
        {"__bytesHash", &bytes_hash},
        {"__bytesToList", &bytes_to_list},
        {"__bytesDecode", &bytes_decode},
        {"__bytesShow", &bytes_show},
        {"__ioEnv", &io_env},
        {"__ioProps", &io_props},
        {"__ioExit", &io_exit},
        {"__ioStderr", &io_stderr},
        {"__procRunInherit", &proc_run_inherit},
        {"__procCapture", &proc_capture},
        {"__procSpawn", &proc_spawn},
        {"__procWait", &proc_wait},
        {"__procKill", &proc_kill},
        {"__tcpConnect", &tcp_connect},
        {"__tcpListen", &tcp_listen},
        {"__tcpAccept", &tcp_accept},
        {"__sockName", &sock_name},
        {"__peerName", &peer_name},
        {"__fdReadLine", &fd_read_line},
        {"__fdRead", &fd_read_chars},
        {"__fdReadBytes", &fd_read_bytes},
        {"__fdReadAll", &fd_read_all},
        {"__fdWrite", &fd_write},
        {"__fdSetTimeout", &fd_set_timeout},
        {"__fdClose", &fd_close},
        {"__tlsConnect", &tls_connect},
        {"__udpBind", &udp_bind},
        {"__udpSend", &udp_send},
        {"__udpReceive", &udp_receive}};
    installAll(ctx, L.globals, globals);
}

}  // namespace protoScala
