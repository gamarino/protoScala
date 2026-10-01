/*
 * FilePrimitives — Track F: the natives behind `scala.io.Source` and the
 * `FileIO` object of lib/prelude.scala.
 *
 * Five globals, all of them hidden (`__` prefixed) and all of them named by
 * `builtinGlobalNames()`:
 *
 *   __fileReadText(path, encoding) -> String    the whole file, decoded
 *   __splitLines(text)             -> List[String]
 *   __fileWriteText(path, text, append) -> Unit
 *   __fileExists(path)             -> Boolean
 *   __fileDelete(path)             -> Boolean
 *
 * The public surface is written in protoScala on top of them, because every
 * decision above the syscall (what `getLines()` returns, whether a closed
 * source may be read again) is a language decision and belongs in the prelude
 * where it is readable.
 *
 * Errors. This file is the boundary between a syscall and a Scala programmer,
 * and a swallowed I/O failure is the worst thing it could do, so **every**
 * syscall's result is checked and every failure leaves through a `ScalaError`
 * whose class name is one the prelude defines (D97) and whose message names the
 * path. The class chosen follows the JVM's own rule, which is simpler than it
 * looks: a failure of `open` is a `FileNotFoundException` whatever its errno
 * (this is why Java reports "Permission denied" and "Is a directory" through
 * that class), and a failure after the descriptor exists is an `IOException`.
 *
 * Messages. The JVM's message is `<path> (<strerror>)`, and `strerror` is
 * localised — on this machine `open` of a missing file reports "No existe el
 * archivo o el directorio". A fixture cannot pin a locale-dependent string, so
 * the reasons this file can produce are spelled out in English in `reasonFor`
 * and only an errno outside that table falls back to `std::strerror` (D98).
 *
 * The I/O track (2026-09-30) added, on protoIO:
 *
 *   __srcOpen(path, encoding) / __srcStdin() -> Int   a streaming source handle
 *   __srcLine(handle)  -> String | null                the next line (strict UTF-8)
 *   __srcRest(handle)  -> String                       everything left
 *   __srcClose(handle) -> Unit
 *   __stdinLine()      -> String | null                StdIn.readLine (lenient UTF-8)
 *   __fileReadBytes(path) / __fileWriteBytes(path, buffer)
 *   __fileList, __fileMkdirs, __fileMove, __fileCopy, __fileStat, __fileRemoveTree
 *
 * Every syscall runs inside `ProtoContext::UnmanagedScope` (IoSupport.h), the
 * Track F ones included: arguments are copied to C++ values first and results
 * are built after the bracket closes.
 */
#include "runtime/IoSupport.h"
#include "runtime/Primitives.h"
#include "runtime/PrimitiveSupport.h"

#include "protoio/file.h"
#include "protoio/stream.h"

#include <atomic>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <BaseTsd.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

// The four path syscalls of this file.  On POSIX they are the plain calls.  On
// Windows a path is UTF-8 here and UTF-16 for the system, files are binary (no
// CRLF translation), sizes are 64-bit, and a directory is reported as EISDIR
// rather than the EACCES _wopen gives for one.
namespace {
#if defined(_WIN32)
using ssize_t = SSIZE_T;
using SysStat = struct ::_stat64;
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC _O_NOINHERIT
#endif

std::wstring widePath(const std::string& path) {
    if (path.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, path.data(), static_cast<int>(path.size()), out.data(), n);
    return out;
}

int sysOpen(const std::string& path, int flags, int /*mode*/ = 0) {
    const std::wstring w = widePath(path);
    const int fd = ::_wopen(w.c_str(), flags | _O_BINARY, _S_IREAD | _S_IWRITE);
    if (fd < 0 && errno == EACCES) {
        const DWORD attrs = ::GetFileAttributesW(w.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) errno = EISDIR;
    }
    return fd;
}
int sysFstat(int fd, SysStat* st) { return ::_fstat64(fd, st); }
int sysStat(const std::string& path, SysStat* st) { return ::_wstat64(widePath(path).c_str(), st); }
int sysUnlink(const std::string& path) { return ::_wunlink(widePath(path).c_str()); }
#else
using SysStat = struct stat;
int sysOpen(const std::string& path, int flags, int mode = 0) { return ::open(path.c_str(), flags, mode); }
int sysFstat(int fd, SysStat* st) { return ::fstat(fd, st); }
int sysStat(const std::string& path, SysStat* st) { return ::stat(path.c_str(), st); }
int sysUnlink(const std::string& path) { return ::unlink(path.c_str()); }
#endif
} // namespace

namespace protoScala {

namespace io {

// ---------------------------------------------------------------------------
// errno, in English (shared with the other I/O primitives, IoSupport.h)
// ---------------------------------------------------------------------------

// The text the JVM's message carries in the C locale, for every errno a file
// operation in this file can produce. An errno outside the table keeps
// `std::strerror`, which is localised: that is the documented fallback (D98),
// not an oversight.
const char* reasonFor(int e) {
    switch (e) {
        case ENOENT:        return "No such file or directory";
        case EACCES:        return "Permission denied";
        case EISDIR:        return "Is a directory";
        case ENOTDIR:       return "Not a directory";
        case EPERM:         return "Operation not permitted";
        case EROFS:         return "Read-only file system";
        case ENOSPC:        return "No space left on device";
#ifdef EDQUOT   // not in the Windows CRT
        case EDQUOT:        return "Disk quota exceeded";
#endif
        case EFBIG:         return "File too large";
        case ENAMETOOLONG:  return "File name too long";
        case ELOOP:         return "Too many levels of symbolic links";
        case EMFILE:        return "Too many open files";
        case ENFILE:        return "Too many open files in system";
        case EIO:           return "Input/output error";
        case ENOTEMPTY:     return "Directory not empty";
        case ETXTBSY:       return "Text file busy";
        case EBUSY:         return "Device or resource busy";
        case ENODEV:        return "No such device";
        case ENXIO:         return "No such device or address";
        case EINVAL:        return "Invalid argument";
        case EBADF:         return "Bad file descriptor";
        case EOVERFLOW:     return "Value too large for defined data type";
        case EEXIST:        return "File exists";
        case EXDEV:         return "Invalid cross-device link";
        case EPIPE:         return "Broken pipe";
        case ECONNREFUSED:  return "Connection refused";
        case ECONNRESET:    return "Connection reset by peer";
        case ECONNABORTED:  return "Software caused connection abort";
        case ETIMEDOUT:     return "Connection timed out";
        case EHOSTUNREACH:  return "No route to host";
        case ENETUNREACH:   return "Network is unreachable";
        case EADDRINUSE:    return "Address already in use";
        case EADDRNOTAVAIL: return "Cannot assign requested address";
        case ENOTCONN:      return "Transport endpoint is not connected";
        case EAGAIN:        return "Resource temporarily unavailable";
        case ECHILD:        return "No child processes";
        case ESRCH:         return "No such process";
        case ENOEXEC:       return "Exec format error";
        case E2BIG:         return "Argument list too long";
        default:            return std::strerror(e);
    }
}

std::string located(const std::string& path, int e) {
    return path + " (" + reasonFor(e) + ")";
}

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

// Strict, exactly as the JVM's decoder is: an overlong encoding, a surrogate
// code point (U+D800..U+DFFF), anything above U+10FFFF and a sequence truncated
// at the end of input are all malformed. `makeString` feeds protoCore's
// `fromUTF8Buffer`, whose contract is well-formed input, so this check is what
// stands between a corrupt file and a corrupt ProtoString.
//
// Answers the length of the well-formed sequence at `i`, or 0 when it is malformed.
static std::size_t sequenceAt(const unsigned char* p, std::size_t n, std::size_t i) {
    const unsigned char c = p[i];
    if (c < 0x80) return 1;
    std::size_t len = 0;
    unsigned int cp = 0;
    if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07u; }
    else return 0;                          // continuation or 0xF8..0xFF
    if (i + len > n) return 0;              // truncated at end of input
    for (std::size_t k = 1; k < len; ++k) {
        const unsigned char cc = p[i + k];
        if ((cc & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    const unsigned int minimum = len == 2 ? 0x80u : (len == 3 ? 0x800u : 0x10000u);
    if (cp < minimum) return 0;                       // overlong
    if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;     // surrogate
    if (cp > 0x10FFFFu) return 0;
    return len;
}

long long firstMalformedUTF8(std::string_view s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const std::size_t len = sequenceAt(p, n, i);
        if (len == 0) return static_cast<long long>(i);
        i += len;
    }
    return -1;
}

std::string lenientUTF8(std::string_view s) {
    if (firstMalformedUTF8(s) < 0) return std::string(s);
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    std::string out;
    out.reserve(n + 16);
    std::size_t i = 0;
    while (i < n) {
        const std::size_t len = sequenceAt(p, n, i);
        if (len == 0) {
            out += "\xEF\xBF\xBD";  // U+FFFD, one per malformed byte
            ++i;
        } else {
            out.append(s.data() + i, len);
            i += len;
        }
    }
    return out;
}

} // namespace io

namespace prim {
namespace {

using io::located;
using io::reasonFor;
using io::firstMalformedUTF8;

[[noreturn]] void openFailed(const std::string& path, int e) {
    // Every failure of `open` is a FileNotFoundException, as on the JVM.
    throw ScalaError("FileNotFoundException", located(path, e));
}

[[noreturn]] void ioFailed(const std::string& path, int e) {
    throw ScalaError("IOException", located(path, e));
}

// A path a protoScala String can hold but a syscall cannot: `c_str()` would
// stop at the NUL and operate on a DIFFERENT file than the program named. That
// must never happen silently.
void checkPath(const std::string& path, const char* what) {
    if (path.find('\0') != std::string::npos)
        throw ScalaError("IllegalArgumentException",
                         std::string(what) + ": a path may not contain a NUL character");
}

// Closes a descriptor on every exit path, including a throw. `close` on the
// read path can fail with nothing left to lose, so its result is ignored there;
// the write path closes explicitly and checks, because that is where a deferred
// write error surfaces (see writeWholeFile).
class FdGuard {
public:
    explicit FdGuard(int fd) : fd_(fd) {}
    ~FdGuard() { if (fd_ >= 0) ::close(fd_); }
    FdGuard(const FdGuard&) = delete;
    FdGuard& operator=(const FdGuard&) = delete;
    int release() { const int fd = fd_; fd_ = -1; return fd; }
private:
    int fd_;
};

// The encodings `Source.fromFile` accepts. protoScala decodes UTF-8 and nothing
// else (D99), and a name it does not implement is refused loudly rather than
// decoded as if it had been UTF-8 all along.
bool namesUtf8(const std::string& enc) {
    std::string up;
    up.reserve(enc.size());
    for (const char c : enc) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    return up == "UTF-8" || up == "UTF8";
}

// ---------------------------------------------------------------------------
// The syscalls
// ---------------------------------------------------------------------------

std::string readWholeFile(const std::string& path, const char* what) {
    checkPath(path, what);
    const int fd = sysOpen(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) openFailed(path, errno);
    FdGuard guard(fd);
    SysStat st {};
    if (sysFstat(fd, &st) != 0) ioFailed(path, errno);
    // On Linux `open` of a directory for reading SUCCEEDS and `read` then fails
    // with EISDIR. The JVM reports a directory from `open`, as a
    // FileNotFoundException carrying "(Is a directory)", so the check is here
    // and the class matches.
    if (S_ISDIR(st.st_mode)) throw ScalaError("FileNotFoundException", path + " (Is a directory)");
    std::string out;
    if (S_ISREG(st.st_mode) && st.st_size > 0)
        out.reserve(static_cast<std::size_t>(st.st_size));
    char buf[65536];
    for (;;) {
        const ssize_t got = ::read(fd, buf, sizeof buf);
        if (got == 0) break;
        if (got < 0) {
            if (errno == EINTR) continue;
            ioFailed(path, errno);
        }
        out.append(buf, static_cast<std::size_t>(got));
    }
    return out;
}

void writeWholeFile(const std::string& path, const std::string& text, bool append,
                    const char* what) {
    checkPath(path, what);
    const int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC);
    const int fd = sysOpen(path, flags, 0666);
    if (fd < 0) openFailed(path, errno);
    FdGuard guard(fd);
    std::size_t off = 0;
    while (off < text.size()) {
        const ssize_t put = ::write(fd, text.data() + off, text.size() - off);
        if (put < 0) {
            if (errno == EINTR) continue;
            ioFailed(path, errno);   // the guard closes the descriptor
        }
        // A short write is normal, not an error: loop until the whole text is
        // out. A partial write that is mistaken for a whole one is exactly the
        // silent failure this file exists to prevent.
        off += static_cast<std::size_t>(put);
    }
    // Closed explicitly and checked: on some filesystems a write error is only
    // reported here, and a `close` whose result is dropped turns a failed write
    // into a successful-looking one.
    if (::close(guard.release()) != 0) ioFailed(path, errno);
}

// ---------------------------------------------------------------------------
// Failures reported by protoIO's file module
// ---------------------------------------------------------------------------

// A protoIO file failure as the prelude exception its kind maps to, with the
// JVM's `<path> (<reason>)` message when there is an errno (D98).
[[noreturn]] void fileFailure(const std::string& path, const protoio::Error& e) {
    const char* cls = e.kind == protoio::Error::Kind::FileNotFound   ? "FileNotFoundException"
                      : e.kind == protoio::Error::Kind::FileExists   ? "FileAlreadyExistsException"
                                                                     : "IOException";
    throw ScalaError(cls, e.sysErrno != 0 ? located(path, e.sysErrno) : io::englishMessage(e));
}

// Runs `f` (plain C++, no ProtoObject) out of the collector's quorum, and
// reports a protoIO failure against `path`.
template <typename F>
auto onFile(ProtoContext* ctx, const std::string& path, F&& f) -> decltype(f()) {
    std::optional<protoio::Error> failed;
    if constexpr (std::is_void_v<decltype(f())>) {
        {
            proto::ProtoContext::UnmanagedScope out(ctx);
            try { f(); } catch (const protoio::Error& e) { failed.emplace(e); }
        }
        if (failed) fileFailure(path, *failed);
    } else {
        std::optional<decltype(f())> r;
        {
            proto::ProtoContext::UnmanagedScope out(ctx);
            try { r.emplace(f()); } catch (const protoio::Error& e) { failed.emplace(e); }
        }
        if (failed) fileFailure(path, *failed);
        return std::move(*r);
    }
}

// ---------------------------------------------------------------------------
// Streaming sources (Source.fromFile, Source.stdin, StdIn.readLine)
// ---------------------------------------------------------------------------

// The reading state of one source, held on the C++ side: it is bytes and a
// descriptor, never a ProtoObject (P1). A source is identified by a handle, an
// Int the prelude's BufferedSource keeps, and not by its descriptor number,
// which the system reuses once the descriptor is closed.
//
// Lines end at "\n", "\r\n" or a lone "\r", as `getLines()` has always split
// them (D100) and as java.io.BufferedReader.readLine splits them.
struct SourceState {
    std::mutex m;             // serialises readers of this source
    int fd = -1;              // -1 once the end of input closed it
    bool ownsFd = true;       // false for standard input
    std::string origin;       // the path, or "<stdin>", for messages
    std::string buf;          // read-ahead; buf[pos..] is unread
    std::size_t pos = 0;
    unsigned long long offset = 0;  // stream offset of buf[pos]
    bool eof = false;
};

struct SourceRegistry {
    std::mutex m;
    std::unordered_map<long long, std::shared_ptr<SourceState>> states;
    std::atomic<long long> next{1};
};

SourceRegistry& sources() {
    static SourceRegistry r;
    return r;
}

// Handle 0 is standard input, shared by every Source.stdin and StdIn.readLine
// so that read-ahead taken by one is seen by the others.
constexpr long long kStdinHandle = 0;

std::shared_ptr<SourceState> stdinState() {
    SourceRegistry& r = sources();
    std::lock_guard<std::mutex> g(r.m);
    auto& slot = r.states[kStdinHandle];
    if (!slot) {
        slot = std::make_shared<SourceState>();
        slot->fd = 0;
        slot->ownsFd = false;
        slot->origin = "<stdin>";
    }
    return slot;
}

std::shared_ptr<SourceState> sourceFor(long long handle) {
    if (handle == kStdinHandle) return stdinState();
    SourceRegistry& r = sources();
    std::lock_guard<std::mutex> g(r.m);
    const auto it = r.states.find(handle);
    if (it == r.states.end())
        throw ScalaError("IllegalStateException", "no open source with handle " + std::to_string(handle));
    return it->second;
}

// Reads one more chunk: whatever has arrived, at most 64 KiB (may block).
// Called with st.m held and inside the bracket; touches no ProtoObject.
//
// This is read(2) and not protoio::readBytes, because readBytes waits until
// it has all the bytes asked for or the stream ends: a source must hand the
// program each line as soon as it arrives (a filter in a pipeline, a file that
// is still growing). The descriptor is opened and closed through protoIO, and
// nothing else reads it, so protoIO's buffer for it stays empty.
void fill(SourceState& st) {
    if (st.eof) return;
    char chunkBuf[65536];
    ssize_t got;
    do {
        got = ::read(st.fd, chunkBuf, sizeof chunkBuf);
    } while (got < 0 && errno == EINTR);
    if (got < 0) {
        const int e = errno;
        throw protoio::Error(protoio::Error::Kind::FileSystem, located(st.origin, e), e);
    }
    const std::optional<std::string> chunk =
        got == 0 ? std::nullopt : std::optional<std::string>(std::string(chunkBuf, static_cast<std::size_t>(got)));
    if (!chunk) {
        st.eof = true;
        // The end of a file closes its descriptor at once, so a program that
        // reads many files to the end without closing them does not run out of
        // descriptors. Standard input stays open.
        if (st.ownsFd) protoio::close(st.fd);
        st.fd = st.ownsFd ? -1 : st.fd;
        return;
    }
    if (st.pos > 0 && st.pos * 2 >= st.buf.size()) {  // compact: consumed prefix
        st.buf.erase(0, st.pos);
        st.pos = 0;
    }
    st.buf += *chunk;
}

struct Piece {
    std::string bytes;
    unsigned long long start = 0;  // stream offset of bytes[0]
};

// The next line without its terminator, or nothing at the end of input.
std::optional<Piece> nextLine(SourceState& st) {
    std::size_t scan = st.pos;
    for (;;) {
        const std::size_t i = st.buf.find_first_of("\r\n", scan);
        if (i != std::string::npos) {
            if (st.buf[i] == '\r' && i + 1 == st.buf.size() && !st.eof) {
                // A CR at the end of what has arrived: whether it is a line of
                // its own or half of a CRLF depends on the next byte.
                const std::size_t rel = i - st.pos;
                fill(st);
                scan = st.pos + rel;
                continue;
            }
            const std::size_t term =
                (st.buf[i] == '\r' && i + 1 < st.buf.size() && st.buf[i + 1] == '\n') ? 2 : 1;
            Piece p{st.buf.substr(st.pos, i - st.pos), st.offset};
            const std::size_t used = i + term - st.pos;
            st.pos += used;
            st.offset += used;
            return p;
        }
        if (st.eof) {
            if (st.pos == st.buf.size()) return std::nullopt;
            Piece p{st.buf.substr(st.pos), st.offset};
            st.offset += p.bytes.size();
            st.buf.clear();
            st.pos = 0;
            return p;
        }
        const std::size_t rel = st.buf.size() - st.pos;
        fill(st);
        scan = st.pos + rel;
    }
}

// Everything left, to the end of input.
Piece rest(SourceState& st) {
    while (!st.eof) fill(st);
    Piece p{st.buf.substr(st.pos), st.offset};
    st.offset += p.bytes.size();
    st.buf.clear();
    st.pos = 0;
    return p;
}

// Reads from a source inside the right bracket: standard input may wait
// without bound (a terminal, a pipe), so it is a blocking call; a file is not.
// Anything printed so far is flushed before the program waits for input, so a
// prompt written with `print` is visible, and a program in a pipeline answers
// each line before it waits for the next.
template <typename F>
auto readSource(ProtoContext* ctx, SourceState& st, F&& f) -> decltype(f()) {
    const bool terminal = !st.ownsFd;
    if (terminal) std::fflush(stdout);
    auto body = [&]() -> decltype(f()) {
        std::lock_guard<std::mutex> g(st.m);
        return f();
    };
    return terminal ? io::blocking(ctx, body) : io::unmanaged(ctx, body);
}

// A strict source's text: well-formed UTF-8, or a MalformedInputException that
// names the source and the stream offset of the first bad byte (D98).
const ProtoObject* strictText(ProtoContext* ctx, const SourceState& st, const Piece& p) {
    const long long bad = firstMalformedUTF8(p.bytes);
    if (bad >= 0)
        throw ScalaError("MalformedInputException",
                         st.origin + ": malformed UTF-8 input at byte " +
                             std::to_string(p.start + static_cast<unsigned long long>(bad)));
    return makeString(ctx, p.bytes);
}

long long handleArg(ProtoContext* ctx, const ProtoList* args, const char* method) {
    return intArg(ctx, arg(ctx, args, 0, method, 1), method);
}

// __srcOpen(path, encoding) -> Int
PRIM(prim_src_open) {
    expectArgs(ctx, args, "Source.fromFile", 2);
    const std::string path = stringArg(ctx, args->getAt(ctx, 0), "Source.fromFile");
    const std::string enc = stringArg(ctx, args->getAt(ctx, 1), "Source.fromFile");
    if (!namesUtf8(enc))
        throw ScalaError("UnsupportedOperationException",
                         "Source.fromFile: protoScala decodes UTF-8 only, not '" + enc + "'");
    checkPath(path, "Source.fromFile");
    int fd = -1, err = 0;
    bool directory = false;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        try {
            fd = protoio::file::open(path, protoio::file::Mode::Read);
            SysStat st {};
            // On Linux `open` of a directory for reading SUCCEEDS and `read` then
            // fails with EISDIR. The JVM reports a directory from `open`, as a
            // FileNotFoundException carrying "(Is a directory)".
            if (sysFstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
                directory = true;
                protoio::close(fd);
            }
        } catch (const protoio::Error& e) {
            err = e.sysErrno != 0 ? e.sysErrno : EIO;
        }
    }
    // Every failure of `open` is a FileNotFoundException, as on the JVM (D97).
    if (err != 0) openFailed(path, err);
    if (directory) throw ScalaError("FileNotFoundException", path + " (Is a directory)");
    auto st = std::make_shared<SourceState>();
    st->fd = fd;
    st->origin = path;
    SourceRegistry& r = sources();
    const long long h = r.next.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> g(r.m);
        r.states[h] = std::move(st);
    }
    return ctx->fromInteger(h);
}

// __srcStdin() -> Int
PRIM(prim_src_stdin) {
    expectArgs(ctx, args, "Source.stdin", 0);
    return ctx->fromInteger(kStdinHandle);
}

// __srcLine(handle) -> String | null, strict UTF-8
PRIM(prim_src_line) {
    const long long h = handleArg(ctx, args, "getLines");
    const std::shared_ptr<SourceState> st = sourceFor(h);
    const std::optional<Piece> p = readSource(ctx, *st, [&] { return nextLine(*st); });
    if (!p) return PROTO_NONE;
    return strictText(ctx, *st, *p);
}

// __srcRest(handle) -> String, strict UTF-8
PRIM(prim_src_rest) {
    const long long h = handleArg(ctx, args, "mkString");
    const std::shared_ptr<SourceState> st = sourceFor(h);
    const Piece p = readSource(ctx, *st, [&] { return rest(*st); });
    return strictText(ctx, *st, p);
}

// __srcClose(handle) -> Unit. Standard input is shared and stays open.
PRIM(prim_src_close) {
    const long long h = handleArg(ctx, args, "close");
    if (h == kStdinHandle) return layoutOf().unit;
    std::shared_ptr<SourceState> st;
    {
        SourceRegistry& r = sources();
        std::lock_guard<std::mutex> g(r.m);
        const auto it = r.states.find(h);
        if (it == r.states.end()) return layoutOf().unit;  // closing twice is harmless
        st = std::move(it->second);
        r.states.erase(it);
    }
    io::unmanaged(ctx, [&] {
        std::lock_guard<std::mutex> g(st->m);
        if (st->fd >= 0 && st->ownsFd) protoio::close(st->fd);
        st->fd = -1;
        st->eof = true;
    });
    return layoutOf().unit;
}

// __stdinLine() -> String | null: StdIn.readLine. Decoded as java.io's
// readers decode a stream: a malformed sequence becomes U+FFFD (D125).
PRIM(prim_stdin_line) {
    expectArgs(ctx, args, "StdIn.readLine", 0);
    const std::shared_ptr<SourceState> st = stdinState();
    const std::optional<Piece> p = readSource(ctx, *st, [&] { return nextLine(*st); });
    if (!p) return PROTO_NONE;
    return makeString(ctx, io::lenientUTF8(p->bytes));
}

// ---------------------------------------------------------------------------
// Whole files: text (Track F) and bytes
// ---------------------------------------------------------------------------

PRIM(prim_file_write_text) {
    (void)self;
    expectArgs(ctx, args, "FileIO.write", 3);
    const std::string path = stringArg(ctx, args->getAt(ctx, 0), "FileIO.write");
    const std::string text = stringArg(ctx, args->getAt(ctx, 1), "FileIO.write");
    const ProtoObject* mode = args->getAt(ctx, 2);
    if (mode != PROTO_TRUE && mode != PROTO_FALSE) wrongType(ctx, "FileIO.write", "a Boolean", mode);
    const bool append = mode == PROTO_TRUE;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        writeWholeFile(path, text, append, "FileIO.write");
    }
    return layoutOf().unit;
}

// __fileReadBytes(path) -> the ProtoByteBuffer a Bytes wraps
PRIM(prim_file_read_bytes) {
    const std::string path =
        stringArg(ctx, arg(ctx, args, 0, "FileIO.readBytes", 1), "FileIO.readBytes");
    std::string data;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        data = readWholeFile(path, "FileIO.readBytes");
    }
    return io::makeByteBuffer(ctx, data);
}

// __fileWriteBytes(path, buffer) -> Unit
PRIM(prim_file_write_bytes) {
    expectArgs(ctx, args, "FileIO.writeBytes", 2);
    const std::string path = stringArg(ctx, args->getAt(ctx, 0), "FileIO.writeBytes");
    const std::string data = io::byteBufferArg(ctx, args->getAt(ctx, 1), "FileIO.writeBytes");
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        writeWholeFile(path, data, false, "FileIO.writeBytes");
    }
    return layoutOf().unit;
}

PRIM(prim_file_exists) {
    (void)self;
    const std::string path = stringArg(ctx, arg(ctx, args, 0, "FileIO.exists", 1), "FileIO.exists");
    checkPath(path, "FileIO.exists");
    SysStat st {};
    int rc;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        rc = sysStat(path, &st);
    }
    // `exists` answers one question only — is there something at this path that
    // can be looked up — so a failure to stat is `false`, as `java.io.File`'s is.
    // It is true for a directory, and false for a dangling symbolic link.
    return boolean(rc == 0);
}

PRIM(prim_file_delete) {
    (void)self;
    const std::string path = stringArg(ctx, arg(ctx, args, 0, "FileIO.delete", 1), "FileIO.delete");
    checkPath(path, "FileIO.delete");
    int rc, e;
    {
        proto::ProtoContext::UnmanagedScope out(ctx);
        rc = sysUnlink(path);
        e = errno;
    }
    if (rc == 0) return PROTO_TRUE;
    // "Nothing was there" is an answer, so it is reported as `false`. Anything
    // else is a failure and must not be reported as one of those two answers:
    // a permission denial or a directory that came back `false` would be a
    // silently swallowed error (D101).
    if (e == ENOENT) return PROTO_FALSE;
    ioFailed(path, e);
}

// ---------------------------------------------------------------------------
// Directories and metadata (protoIO's file module)
// ---------------------------------------------------------------------------

// __fileStat(path) -> List(isFile, isDirectory, size, modifiedMs) | null
PRIM(prim_file_stat) {
    const std::string path = stringArg(ctx, arg(ctx, args, 0, "FileIO.stat", 1), "FileIO.stat");
    checkPath(path, "FileIO");
    const std::optional<protoio::file::Stat> st =
        onFile(ctx, path, [&] { return protoio::file::stat(path); });
    if (!st) return PROTO_NONE;
    ListBuilder out(ctx);
    out.add(boolean(st->isFile));
    out.add(boolean(st->isDirectory));
    out.add(out.context()->fromInteger(st->size));
    out.add(out.context()->fromInteger(st->modifiedMs));
    return out.finish();
}

// __fileList(path) -> List[String], sorted
PRIM(prim_file_list) {
    const std::string path = stringArg(ctx, arg(ctx, args, 0, "FileIO.list", 1), "FileIO.list");
    checkPath(path, "FileIO.list");
    const std::vector<std::string> names = onFile(ctx, path, [&] {
        const std::optional<protoio::file::Stat> st = protoio::file::stat(path);
        if (!st) throw protoio::Error(protoio::Error::Kind::FileNotFound, path, ENOENT);
        if (!st->isDirectory) throw protoio::Error(protoio::Error::Kind::FileSystem, path, ENOTDIR);
        return protoio::file::list(path);
    });
    ListBuilder out(ctx);
    for (const std::string& n : names) out.add(makeString(out.context(), io::lenientUTF8(n)));
    return out.finish();
}

// __fileMkdirs(path) -> Boolean: true when it created the directory, false
// when one was already there. Anything else at the path, or a failure, raises.
PRIM(prim_file_mkdirs) {
    const std::string path = stringArg(ctx, arg(ctx, args, 0, "FileIO.mkdirs", 1), "FileIO.mkdirs");
    checkPath(path, "FileIO.mkdirs");
    const int outcome = onFile(ctx, path, [&] {
        const std::optional<protoio::file::Stat> st = protoio::file::stat(path);
        if (st) return st->isDirectory ? 0 : -1;
        protoio::file::mkdir(path, /*parents=*/true);
        return 1;
    });
    if (outcome < 0) throw ScalaError("FileAlreadyExistsException", path);
    return boolean(outcome == 1);
}

// __fileMove(from, to, replace) / __fileCopy(from, to, replace) -> Unit.
// java.nio.file.Files' rule: an existing target is refused with a
// FileAlreadyExistsException naming it, unless the caller asked to replace it.
const ProtoObject* moveOrCopy(ProtoContext* ctx, const ProtoList* args, const char* method, bool copy) {
    expectArgs(ctx, args, method, 3);
    const std::string from = stringArg(ctx, args->getAt(ctx, 0), method);
    const std::string to = stringArg(ctx, args->getAt(ctx, 1), method);
    const ProtoObject* rep = args->getAt(ctx, 2);
    if (rep != PROTO_TRUE && rep != PROTO_FALSE) wrongType(ctx, method, "a Boolean", rep);
    checkPath(from, method);
    checkPath(to, method);
    const int outcome = onFile(ctx, from, [&] {
        if (!protoio::file::stat(from)) return -1;
        if (rep == PROTO_FALSE && protoio::file::stat(to)) return -2;
        if (copy) protoio::file::copy(from, to);
        else protoio::file::move(from, to);
        return 0;
    });
    if (outcome == -1) throw ScalaError("FileNotFoundException", located(from, ENOENT));
    if (outcome == -2) throw ScalaError("FileAlreadyExistsException", to);
    return layoutOf().unit;
}

PRIM(prim_file_move) { return moveOrCopy(ctx, args, "FileIO.move", false); }
PRIM(prim_file_copy) { return moveOrCopy(ctx, args, "FileIO.copy", true); }

// __fileRemoveTree(path) -> Boolean: whether anything was there.
PRIM(prim_file_remove_tree) {
    const std::string path = stringArg(ctx, arg(ctx, args, 0, "FileIO.deleteRecursively", 1),
                                       "FileIO.deleteRecursively");
    checkPath(path, "FileIO.deleteRecursively");
    // An empty path or "/" would be a catastrophe, not a request.
    if (path.empty() || path.find_first_not_of('/') == std::string::npos)
        throw ScalaError("IllegalArgumentException",
                         "FileIO.deleteRecursively: refusing to remove '" + path + "'");
    return boolean(onFile(ctx, path, [&] { return protoio::file::remove(path, /*recursive=*/true); }));
}

// __splitLines(text): the line splitter behind `Source.fromString(...)`.
// Scala's `getLines()` breaks on "\n", "\r\n" and "\r", strips the terminator,
// and a single trailing terminator does NOT produce a final empty line --
// verified against scalac 3.9.0 (see the plan's table).
PRIM(prim_split_lines) {
    (void)self;
    const std::string text = stringArg(ctx, arg(ctx, args, 0, "__splitLines", 1), "__splitLines");
    ListBuilder out(ctx);
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        std::size_t j = i;
        while (j < n && text[j] != '\n' && text[j] != '\r') ++j;
        out.add(makeString(out.context(), text.substr(i, j - i)));
        if (j == n) break;
        // "\r\n" is ONE terminator.
        i = (text[j] == '\r' && j + 1 < n && text[j + 1] == '\n') ? j + 2 : j + 1;
    }
    return out.finish();
}

// __skipLines(text, k): what is left of `text` after its first k lines and
// their terminators, split as __splitLines splits them. A string source's
// mkString after some of its lines were read.
PRIM(prim_skip_lines) {
    expectArgs(ctx, args, "__skipLines", 2);
    const std::string text = stringArg(ctx, args->getAt(ctx, 0), "__skipLines");
    long long k = intArg(ctx, args->getAt(ctx, 1), "__skipLines");
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (k > 0 && i < n) {
        std::size_t j = i;
        while (j < n && text[j] != '\n' && text[j] != '\r') ++j;
        if (j == n) { i = n; break; }
        i = (text[j] == '\r' && j + 1 < n && text[j + 1] == '\n') ? j + 2 : j + 1;
        --k;
    }
    return makeString(ctx, std::string_view(text).substr(i));
}

#undef PRIM

}  // namespace
}  // namespace prim

void installFilePrimitives(proto::ProtoContext* ctx, const RuntimeLayout& L) {
    using namespace protoScala::prim;
    static constexpr MethodEntry globals[] = {
        {"__fileWriteText", &prim_file_write_text},
        {"__fileExists", &prim_file_exists},
        {"__fileDelete", &prim_file_delete},
        {"__splitLines", &prim_split_lines},
        {"__skipLines", &prim_skip_lines},
        {"__srcOpen", &prim_src_open},
        {"__srcStdin", &prim_src_stdin},
        {"__srcLine", &prim_src_line},
        {"__srcRest", &prim_src_rest},
        {"__srcClose", &prim_src_close},
        {"__stdinLine", &prim_stdin_line},
        {"__fileReadBytes", &prim_file_read_bytes},
        {"__fileWriteBytes", &prim_file_write_bytes},
        {"__fileStat", &prim_file_stat},
        {"__fileList", &prim_file_list},
        {"__fileMkdirs", &prim_file_mkdirs},
        {"__fileMove", &prim_file_move},
        {"__fileCopy", &prim_file_copy},
        {"__fileRemoveTree", &prim_file_remove_tree}};
    installAll(ctx, L.globals, globals);
}

}  // namespace protoScala
