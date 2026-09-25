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
 */
#include "runtime/Primitives.h"
#include "runtime/PrimitiveSupport.h"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <string>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace protoScala {
namespace prim {
namespace {

// ---------------------------------------------------------------------------
// errno, in English
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
        case EDQUOT:        return "Disk quota exceeded";
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
        default:            return std::strerror(e);
    }
}

std::string located(const std::string& path, int e) {
    return path + " (" + reasonFor(e) + ")";
}

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

// ---------------------------------------------------------------------------
// UTF-8
// ---------------------------------------------------------------------------

// The byte offset of the first malformed byte, or -1 when the whole string is
// well-formed UTF-8. Strict, exactly as the JVM's decoder is: an overlong
// encoding, a surrogate code point (U+D800..U+DFFF), anything above U+10FFFF and
// a sequence truncated at the end of input are all malformed. `makeString` feeds
// protoCore's `fromUTF8Buffer`, whose contract is well-formed input, so this
// check is what stands between a corrupt file and a corrupt ProtoString.
long long firstMalformedUTF8(const std::string& s) {
    const auto* p = reinterpret_cast<const unsigned char*>(s.data());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        const unsigned char c = p[i];
        if (c < 0x80) { ++i; continue; }
        std::size_t len = 0;
        unsigned int cp = 0;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1Fu; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0Fu; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07u; }
        else return static_cast<long long>(i);            // continuation or 0xF8..0xFF
        if (i + len > n) return static_cast<long long>(i); // truncated at end of input
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char cc = p[i + k];
            if ((cc & 0xC0) != 0x80) return static_cast<long long>(i);
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        const unsigned int minimum = len == 2 ? 0x80u : (len == 3 ? 0x800u : 0x10000u);
        if (cp < minimum) return static_cast<long long>(i);                 // overlong
        if (cp >= 0xD800u && cp <= 0xDFFFu) return static_cast<long long>(i); // surrogate
        if (cp > 0x10FFFFu) return static_cast<long long>(i);
        i += len;
    }
    return -1;
}

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

std::string readWholeFile(const std::string& path) {
    checkPath(path, "Source.fromFile");
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) openFailed(path, errno);
    FdGuard guard(fd);
    struct stat st {};
    if (::fstat(fd, &st) != 0) ioFailed(path, errno);
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

void writeWholeFile(const std::string& path, const std::string& text, bool append) {
    checkPath(path, "FileIO.write");
    const int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC);
    const int fd = ::open(path.c_str(), flags, 0666);
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
// The primitives
// ---------------------------------------------------------------------------

PRIM(prim_file_read_text) {
    (void)self;
    expectArgs(ctx, args, "Source.fromFile", 2);
    const std::string path = stringArg(ctx, args->getAt(ctx, 0), "Source.fromFile");
    const std::string enc = stringArg(ctx, args->getAt(ctx, 1), "Source.fromFile");
    if (!namesUtf8(enc))
        throw ScalaError("UnsupportedOperationException",
                         "Source.fromFile: protoScala decodes UTF-8 only, not '" + enc + "'");
    const std::string text = readWholeFile(path);
    const long long bad = firstMalformedUTF8(text);
    if (bad >= 0)
        throw ScalaError("MalformedInputException",
                         path + ": malformed UTF-8 input at byte " + std::to_string(bad));
    return makeString(ctx, text);
}

PRIM(prim_file_write_text) {
    (void)self;
    expectArgs(ctx, args, "FileIO.write", 3);
    const std::string path = stringArg(ctx, args->getAt(ctx, 0), "FileIO.write");
    const std::string text = stringArg(ctx, args->getAt(ctx, 1), "FileIO.write");
    const ProtoObject* mode = args->getAt(ctx, 2);
    if (mode != PROTO_TRUE && mode != PROTO_FALSE) wrongType(ctx, "FileIO.write", "a Boolean", mode);
    const bool append = mode == PROTO_TRUE;
    writeWholeFile(path, text, append);
    return layoutOf().unit;
}

PRIM(prim_file_exists) {
    (void)self;
    const std::string path = stringArg(ctx, arg(ctx, args, 0, "FileIO.exists", 1), "FileIO.exists");
    checkPath(path, "FileIO.exists");
    struct stat st {};
    // `exists` answers one question only — is there something at this path that
    // can be looked up — so a failure to stat is `false`, as `java.io.File`'s is.
    // It is true for a directory, and false for a dangling symbolic link.
    return boolean(::stat(path.c_str(), &st) == 0);
}

PRIM(prim_file_delete) {
    (void)self;
    const std::string path = stringArg(ctx, arg(ctx, args, 0, "FileIO.delete", 1), "FileIO.delete");
    checkPath(path, "FileIO.delete");
    if (::unlink(path.c_str()) == 0) return PROTO_TRUE;
    // "Nothing was there" is an answer, so it is reported as `false`. Anything
    // else is a failure and must not be reported as one of those two answers:
    // a permission denial or a directory that came back `false` would be a
    // silently swallowed error (D101).
    if (errno == ENOENT) return PROTO_FALSE;
    ioFailed(path, errno);
}

// __splitLines(text): the line splitter behind `BufferedSource.getLines()`,
// shared by `Source.fromFile` and `Source.fromString` so both answer the same
// way. Scala's `getLines()` breaks on "\n", "\r\n" and "\r", strips the
// terminator, and a single trailing terminator does NOT produce a final empty
// line — verified against scalac 3.9.0 (see the plan's table).
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

#undef PRIM

}  // namespace
}  // namespace prim

void installFilePrimitives(proto::ProtoContext* ctx, const RuntimeLayout& L) {
    using namespace protoScala::prim;
    static constexpr MethodEntry globals[] = {
        {"__fileReadText", &prim_file_read_text},
        {"__fileWriteText", &prim_file_write_text},
        {"__fileExists", &prim_file_exists},
        {"__fileDelete", &prim_file_delete},
        {"__splitLines", &prim_split_lines}};
    installAll(ctx, L.globals, globals);
}

}  // namespace protoScala
