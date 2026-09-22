/*
 * Hashing — Scala's hash functions, bit for bit: MurmurHash3 as in
 * scala.util.hashing.MurmurHash3 / scala.runtime.Statics, and
 * java.lang.String/Double/Float.hashCode. The case-class and tuple values were
 * confirmed with scalac 3.9.0 (Phase 2 plan, Design note 11). Pure functions.
 */
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace protoScala::hashing {

inline constexpr std::uint32_t kProductSeed = 0xcafebabeu;  // MurmurHash3.productSeed

inline std::uint32_t rotl(std::uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }

inline std::uint32_t mixLast(std::uint32_t h, std::uint32_t k) {
    k *= 0xcc9e2d51u;
    k = rotl(k, 15);
    k *= 0x1b873593u;
    return h ^ k;
}

inline std::uint32_t mix(std::uint32_t h, std::uint32_t data) {
    h = mixLast(h, data);
    h = rotl(h, 13);
    return h * 5u + 0xe6546b64u;
}

inline std::uint32_t avalanche(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

inline std::uint32_t finalizeHash(std::uint32_t h, std::uint32_t length) {
    return avalanche(h ^ length);
}

// java.lang.String.hashCode over the UTF-16 code units of a UTF-8 string.
inline std::int32_t javaStringHash(const std::string& utf8) {
    std::uint32_t h = 0;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const unsigned char b = static_cast<unsigned char>(utf8[i]);
        std::uint32_t cp;
        std::size_t len;
        if (b < 0x80)      { cp = b;        len = 1; }
        else if (b < 0xE0) { cp = b & 0x1F; len = 2; }
        else if (b < 0xF0) { cp = b & 0x0F; len = 3; }
        else               { cp = b & 0x07; len = 4; }
        for (std::size_t k = 1; k < len && i + k < utf8.size(); ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
        i += len;
        if (cp >= 0x10000) {
            cp -= 0x10000;
            h = 31u * h + (0xD800u + (cp >> 10));
            h = 31u * h + (0xDC00u + (cp & 0x3FFu));
        } else {
            h = 31u * h + cp;
        }
    }
    return static_cast<std::int32_t>(h);
}

// Scala 3's synthesised hashCode of a case class or tuple: seed 0xcafebabe,
// the productPrefix's hash, each element's ##, finalizeHash(h, arity).
// Arity 0 (case objects, `Empty()`): the prefix's hash.
inline std::int32_t productHash(std::int32_t prefixHash, const std::vector<std::int32_t>& elements) {
    if (elements.empty()) return prefixHash;
    std::uint32_t h = mix(kProductSeed, static_cast<std::uint32_t>(prefixHash));
    for (std::int32_t e : elements) h = mix(h, static_cast<std::uint32_t>(e));
    return static_cast<std::int32_t>(finalizeHash(h, static_cast<std::uint32_t>(elements.size())));
}

// MurmurHash3.orderedHash with seqSeed ("Seq".hashCode). Scala's List uses a
// specialised linear-sequence hash, so List hash codes are consistent with ==
// but not equal to the JVM's (STATUS, Phase 2 notes).
inline std::int32_t seqHash(const std::vector<std::int32_t>& elements) {
    std::uint32_t h = static_cast<std::uint32_t>(javaStringHash("Seq"));
    for (std::int32_t e : elements) h = mix(h, static_cast<std::uint32_t>(e));
    return static_cast<std::int32_t>(finalizeHash(h, static_cast<std::uint32_t>(elements.size())));
}

// Statics.longHash: an Int-range value hashes to itself, otherwise Long.hashCode.
inline std::int32_t longHash(long long v) {
    const auto i = static_cast<std::int32_t>(v);
    if (static_cast<long long>(i) == v) return i;
    const auto u = static_cast<unsigned long long>(v);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(u ^ (u >> 32)));
}

inline std::int32_t javaDoubleHash(double d) {
    std::uint64_t bits;
    if (std::isnan(d)) bits = 0x7ff8000000000000ULL;  // doubleToLongBits canonical NaN
    else std::memcpy(&bits, &d, sizeof bits);
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(bits ^ (bits >> 32)));
}

inline std::int32_t javaFloatHash(float f) {
    std::uint32_t bits;
    if (std::isnan(f)) bits = 0x7fc00000u;
    else std::memcpy(&bits, &f, sizeof bits);
    return static_cast<std::int32_t>(bits);
}

// Statics.doubleHash: the `##` of a Double.
inline std::int32_t doubleHash(double d) {
    if (std::isnan(d)) return javaDoubleHash(d);
    if (d >= -2147483648.0 && d <= 2147483647.0 && d == std::trunc(d))
        return static_cast<std::int32_t>(d);
    if (d >= -9223372036854775808.0 && d < 9223372036854775808.0 && d == std::trunc(d))
        return longHash(static_cast<long long>(d));
    const float f = static_cast<float>(d);
    if (static_cast<double>(f) == d) return javaFloatHash(f);
    return javaDoubleHash(d);
}

} // namespace protoScala::hashing
