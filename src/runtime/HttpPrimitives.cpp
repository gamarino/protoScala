/*
 * HttpPrimitives — the natives behind the prelude's HTTP surface: the client
 * (`Requests`, requests-scala's shape) and the server (`HttpServer`, whose
 * accept loop and dispatch to actors are written in the prelude).
 *
 * The HTTP message layer is protoIO's (protoio/http.h): limits, header
 * validation, chunked bodies, redirects and their policy are the library's, so
 * they are the same in every protoCore runtime. This file only converts values
 * and brackets each call (IoSupport.h). The client is ONE native call, one
 * bracket, for the whole exchange including redirects.
 *
 *   __httpRequest(method, url, headers, body, timeoutMs, maxRedirects)
 *       -> List(status, reason, headers, bodyBuffer)
 *   __httpReadRequest(fd, maxBody)
 *       -> List(method, target, path, query, headers, bodyBuffer) | null
 *   __httpWriteResponse(fd, status, headers, body) -> Unit
 *   __httpParseQuery(text) -> List(name, value, ...)
 *   __urlEncode(text) -> String
 *
 * Header lists and query maps cross as flat lists (name, value, name, value,
 * ...); the prelude turns them into Maps.
 */
#include "runtime/IoSupport.h"
#include "runtime/Primitives.h"
#include "runtime/PrimitiveSupport.h"

#include "protoio/http.h"
#include "protoio/stream.h"

#include <optional>
#include <string>
#include <vector>

namespace protoScala {
namespace prim {
namespace {

namespace http = protoio::http;

// A flat list of Strings (name, value, ...) as protoIO headers.
http::Headers headersArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    const SeqView sv = seqViewOf(ctx, layoutOf(), v);
    if (!sv.valid || sv.size % 2 != 0) wrongType(ctx, method, "a flat list of header names and values", v);
    http::Headers out;
    for (long long i = 0; i + 1 < sv.size; i += 2)
        out.push_back({stringArg(ctx, seqElemAt(ctx, sv, i), method),
                       stringArg(ctx, seqElemAt(ctx, sv, i + 1), method)});
    return out;
}

std::string octetsArg(ProtoContext* ctx, const ProtoObject* v, const char* method) {
    if (proto::ProtoObject::isStringTagFast(v)) return asStr(v)->toStdString(ctx);
    return io::byteBufferArg(ctx, v, method);
}

const ProtoObject* text(ProtoContext* ctx, std::string_view bytes) {
    return makeString(ctx, io::lenientUTF8(bytes));
}

// Headers (or query pairs) as a flat list of Strings.
template <typename Pairs, typename Name, typename Value>
const ProtoObject* flatList(ProtoContext* ctx, const Pairs& pairs, Name name, Value value) {
    ListBuilder out(ctx);
    for (const auto& p : pairs) {
        out.add(text(out.context(), name(p)));
        out.add(text(out.context(), value(p)));
    }
    return out.finish();
}

const ProtoObject* headerList(ProtoContext* ctx, const http::Headers& h) {
    return flatList(ctx, h, [](const http::Header& x) -> const std::string& { return x.name; },
                    [](const http::Header& x) -> const std::string& { return x.value; });
}

const ProtoObject* pairList(ProtoContext* ctx,
                            const std::vector<std::pair<std::string, std::string>>& q) {
    return flatList(ctx, q, [](const auto& x) -> const std::string& { return x.first; },
                    [](const auto& x) -> const std::string& { return x.second; });
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

PRIM(http_request) {
    expectArgs(ctx, args, "Requests.send", 6);
    http::Request rq;
    rq.method = stringArg(ctx, args->getAt(ctx, 0), "Requests.send");
    rq.url = stringArg(ctx, args->getAt(ctx, 1), "Requests.send");
    rq.headers = headersArg(ctx, args->getAt(ctx, 2), "Requests.send");
    const ProtoObject* body = args->getAt(ctx, 3);
    if (body && body != PROTO_NONE) rq.body = octetsArg(ctx, body, "Requests.send");
    rq.timeoutMs = static_cast<int>(intArg(ctx, args->getAt(ctx, 4), "Requests.send"));
    rq.maxRedirects = static_cast<int>(intArg(ctx, args->getAt(ctx, 5), "Requests.send"));
    rq.userAgent = "protoScala";
    const http::Response r = io::blocking(ctx, [&] { return http::httpRequest(rq); });
    ListBuilder out(ctx);
    out.add(out.context()->fromInteger(r.status));
    out.add(text(out.context(), r.reason));
    out.add(headerList(out.context(), r.headers));
    out.add(io::makeByteBuffer(out.context(), r.body));
    return out.finish();
}

// ---------------------------------------------------------------------------
// Server
// ---------------------------------------------------------------------------

// A connection is given this long to send its request, and each later wait
// on it is bounded the same way, so a client that stalls cannot hold a
// serving actor forever.
constexpr int kConnectionTimeoutMs = 30000;

struct Incoming {
    bool present = false;
    http::RequestHead head;
    std::string path;
    std::vector<std::pair<std::string, std::string>> query;
    std::string body;
};

// Answers a request the library refused, before any handler runs, and reads
// what the client may still be sending: closing a socket with unread input
// resets the connection, which can destroy the response in flight.
void refuse(int fd, int status, const std::string& why) {
    try {
        http::ResponseHead rh;
        rh.status = status;
        rh.headers.push_back({"content-type", "text/plain; charset=utf-8"});
        http::writeResponse(fd, rh, http::reasonPhrase(status) + ": " + why + "\n");
        protoio::setTimeout(fd, 200);
        std::size_t drained = 0;
        while (drained < (1u << 20)) {
            const std::optional<std::string> more = protoio::readBytes(fd, 65536);
            if (!more) break;
            drained += more->size();
        }
    } catch (const protoio::Error&) {
        // The client went away; there is nobody to tell.
    }
}

PRIM(http_read_request) {
    expectArgs(ctx, args, "HttpServer", 2);
    const int fd = static_cast<int>(intArg(ctx, args->getAt(ctx, 0), "HttpServer"));
    const long long maxBody = intArg(ctx, args->getAt(ctx, 1), "HttpServer");
    const Incoming in = io::blocking(ctx, [&] {
        Incoming r;
        http::Limits limits;
        limits.maxBody = static_cast<std::size_t>(maxBody > 0 ? maxBody : 0);
        protoio::setTimeout(fd, kConnectionTimeoutMs);
        try {
            std::optional<http::RequestHead> head = http::readRequestHead(fd, limits);
            if (!head) return r;
            r.body = http::readBody(fd, head->headers, limits.maxBody);
            const std::string& t = head->target;
            const std::size_t q = t.find('?');
            r.path = http::percentDecode(std::string_view(t).substr(0, q));
            if (q != std::string::npos) r.query = http::parseQuery(std::string_view(t).substr(q + 1));
            r.head = std::move(*head);
            r.present = true;
        } catch (const http::HttpRefusal& e) {
            refuse(fd, e.status, e.what());
        } catch (const protoio::Error& e) {
            using K = protoio::Error::Kind;
            // A chunked body over the limit, or a body that is malformed or
            // ends early: refused like the head's own limits.
            if (e.kind == K::BodyTooLarge) refuse(fd, 413, e.what());
            else if (e.kind == K::Network || e.kind == K::LineTooLong) refuse(fd, 400, e.what());
            // Otherwise the client timed out or went away: nothing to answer.
        }
        return r;
    });
    if (!in.present) return PROTO_NONE;
    ListBuilder out(ctx);
    out.add(text(out.context(), in.head.method));
    out.add(text(out.context(), in.head.target));
    out.add(text(out.context(), in.path));
    out.add(pairList(out.context(), in.query));
    out.add(headerList(out.context(), in.head.headers));
    out.add(io::makeByteBuffer(out.context(), in.body));
    return out.finish();
}

// __httpWriteResponse(fd, status, headers, body): validated by the library
// before anything is written, so a header with a line break raises
// IllegalArgumentException and nothing reaches the client.
PRIM(http_write_response) {
    expectArgs(ctx, args, "HttpServer", 4);
    const int fd = static_cast<int>(intArg(ctx, args->getAt(ctx, 0), "HttpServer"));
    http::ResponseHead rh;
    rh.status = static_cast<int>(intArg(ctx, args->getAt(ctx, 1), "Response"));
    rh.headers = headersArg(ctx, args->getAt(ctx, 2), "Response");
    const ProtoObject* bodyValue = args->getAt(ctx, 3);
    const bool isText = proto::ProtoObject::isStringTagFast(bodyValue);
    const std::string body = octetsArg(ctx, bodyValue, "Response");
    if (!http::find(rh.headers, "content-type"))
        rh.headers.push_back({"content-type", isText ? "text/plain; charset=utf-8"
                                                     : "application/octet-stream"});
    io::blocking(ctx, [&] { http::writeResponse(fd, rh, body); });
    return layoutOf().unit;
}

PRIM(http_parse_query) {
    const std::string s = stringArg(ctx, arg(ctx, args, 0, "form", 1), "form");
    return pairList(ctx, http::parseQuery(s));
}

// application/x-www-form-urlencoded, as java.net.URLEncoder encodes it: the
// unreserved characters stay, a space is '+', every other byte is %XX.
PRIM(url_encode) {
    const std::string s = stringArg(ctx, arg(ctx, args, 0, "urlEncode", 1), "urlEncode");
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (const unsigned char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '*') {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return makeString(ctx, out);
}

#undef PRIM

}  // namespace
}  // namespace prim

void installHttpPrimitives(proto::ProtoContext* ctx, const RuntimeLayout& L) {
    using namespace protoScala::prim;
    static constexpr MethodEntry globals[] = {
        {"__httpRequest", &http_request},
        {"__httpReadRequest", &http_read_request},
        {"__httpWriteResponse", &http_write_response},
        {"__httpParseQuery", &http_parse_query},
        {"__urlEncode", &url_encode}};
    installAll(ctx, L.globals, globals);
}

}  // namespace protoScala
