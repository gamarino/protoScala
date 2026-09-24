/*
 * Format — the runtime half of the `f` interpolator (Phase 3, plan A0-2, D55).
 *
 * `parseFormatSpec` lives in src/support/FormatSpec.h and is free of protoCore,
 * so the compiler validates a specifier and reports a bad one at its own source
 * position. `formatOne` renders a value and needs both.
 */
#pragma once
#include "support/FormatSpec.h"
#include "protoCore.h"

#include <string>

namespace protoScala {
struct RuntimeLayout;

// Renders one value under `spec`. `L` is needed for show()/typeName(), which the
// `%s` conversion and every error path use. Raises a Scala
// IllegalArgumentException when the value does not fit the conversion: protoScala
// has no static types, so what scalac catches at compile time is caught here
// (D4).
std::string formatOne(proto::ProtoContext* ctx, const RuntimeLayout& L, const FormatSpec& spec,
                      const proto::ProtoObject* v);

} // namespace protoScala
