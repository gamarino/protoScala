// One conformance case per process, for the case whose failure mode is
// std::abort() inside ProtoSpace::waitForHeapHeadroom.  An abort would take a
// whole test binary with it, so runAll() skips that case in-process and names
// this binary in the skip message.
#include "ConformanceHost.h"

#include <protoCoreConformance.h>

PROTOCORE_CONFORMANCE_ISOLATE_MAIN(protoScala::test::ScalaConformanceHost)
