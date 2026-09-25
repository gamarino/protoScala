// protoScala runs protoCore's embedder conformance suite (P4).
//
// Three lines, by design: the cases live in protoCore, protoScala supplies the
// adaptor, and GoogleTest does the asserting.  One ctest entry per case, so a
// failure names the rule it violated and the first run produces the COMPLETE
// list rather than stopping at the first red.
#include "ConformanceHost.h"

#include <protoCoreConformanceGTest.h>

using protoScala::test::ScalaConformanceHost;

PROTOCORE_CONFORMANCE_GTEST(ScalaConformanceHost)
