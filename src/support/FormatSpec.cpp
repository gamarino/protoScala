#include "support/FormatSpec.h"

#include <stdexcept>

namespace protoScala {

FormatSpec parseFormatSpec(const std::string& spec) {
    FormatSpec out;
    if (spec.empty()) return out;                      // no specifier: %s
    if (spec[0] != '%') throw std::invalid_argument("a format specifier starts with '%'");
    std::size_t k = 1;
    for (; k < spec.size(); ++k) {
        if (spec[k] == '-') out.leftAlign = true;
        else if (spec[k] == '+') out.plusSign = true;
        else if (spec[k] == ' ') out.spaceSign = true;
        else if (spec[k] == '0') out.zeroPad = true;
        else if (spec[k] == ',') out.grouping = true;
        else if (spec[k] == '#') out.alternate = true;
        else break;
    }
    std::size_t digits = k;
    while (k < spec.size() && spec[k] >= '0' && spec[k] <= '9') ++k;
    if (k > digits) out.width = std::stoi(spec.substr(digits, k - digits));
    if (k < spec.size() && spec[k] == '.') {
        ++k;
        digits = k;
        while (k < spec.size() && spec[k] >= '0' && spec[k] <= '9') ++k;
        out.precision = k > digits ? std::stoi(spec.substr(digits, k - digits)) : 0;
    }
    if (k + 1 != spec.size())
        throw std::invalid_argument("unsupported format specifier '" + spec + "'");
    out.conversion = spec[k];
    switch (out.conversion) {
        case 's': case 'b': case 'c': case 'd': case 'o': case 'x': case 'X':
        case 'e': case 'E': case 'f': case 'g': case 'G':
            break;
        default:
            throw std::invalid_argument("unsupported format specifier '" + spec + "'");
    }
    return out;
}

} // namespace protoScala
