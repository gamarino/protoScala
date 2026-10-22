/*
 * A provider plug-in (INTEROP §3.1) that brings protoCore's example geometry
 * module (protoCore/examples/exceptions/geometry_module) into a protoScala
 * process. It is built only by the test tree and is never installed.
 *
 * The module itself is protoCore's, unchanged, linked as its own shared library
 * (libprotocore_example_geometry). This file adds nothing but the two plug-in
 * symbols: it registers the module's provider (alias `example_geometry`) and
 * appends `provider:example_geometry` to the space's resolution chain, which is
 * what GeometryModule.h asks an embedder to do. A protoScala program then reaches
 * it with an ordinary, unprefixed import, resolved through the chain:
 *
 *     import example.geometry as g
 *
 * It exists for tests/conformance/25-interop/geometry-module-errors.scala: the
 * module defines its own error types (GeometryError, NegativeRadius,
 * DegenerateTriangle), raises them as proto::ProtoException, and a Scala
 * handler must select them by prototype.
 */
#include "protoCore.h"
#include "umd/ProviderPlugins.h"

#include "GeometryModule.h"

extern "C" PROTOSCALA_PROVIDER_EXPORT const char* protoScalaProviderABI() {
    return protoScala::kProviderPluginABI;
}

extern "C" PROTOSCALA_PROVIDER_EXPORT int protoScalaRegisterProviders(proto::ProtoSpace* space) {
    example::registerGeometryProvider();
    if (!space) return 1;
    // APPEND, never replace: the chain already holds provider:scala and whatever
    // an embedder put there (ScalaModuleProvider.cpp, installScalaProvider).
    proto::ProtoContext ctx(space, space->rootContext);
    const proto::ProtoObject* chainObj = space->getResolutionChain();
    const proto::ProtoList* chain =
        (chainObj && chainObj != PROTO_NONE) ? chainObj->asList(&ctx) : nullptr;
    if (!chain) chain = ctx.newList();
    chain = chain->appendLast(
        &ctx, proto::ProtoString::createSymbol(&ctx, "provider:example_geometry")->asObject(&ctx));
    space->setResolutionChain(chain->asObject(&ctx));
    return 0;
}
