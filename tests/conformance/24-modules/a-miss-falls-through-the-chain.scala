// EXPECT-ERROR: no module found for 'util.Nope' (tried
// The message lists every candidate path, which is what proves the provider
// returned PROTO_NONE and the resolver walked on rather than stopping at the
// first entry of the chain.
import util.Nope
@main def run(): Unit = println(1)
