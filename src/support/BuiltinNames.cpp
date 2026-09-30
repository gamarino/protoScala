#include "support/BuiltinNames.h"
#include "compiler/ClassInfo.h"

namespace protoScala {

const std::vector<std::string>& builtinGlobalNames() {
    // The TupleN companions are globals too: `Tuple2(1, 2)` is `(1, 2)`.
    static const std::vector<std::string> names = [] {
        std::vector<std::string> v = {"println", "print", "List", "Nil", "__raise",
                                      "Actor", "Future", "Thread", "System",
                                      "__fmt", "__tryOf", "__classNameOf", "__kwprobe",
                                      "__installExtension",
                                      "Vector", "Map", "Set",
                                      // Track F: the filesystem natives the
                                      // prelude's Source and FileIO call.
                                      "__fileWriteText",
                                      "__fileExists", "__fileDelete", "__splitLines", "__skipLines",
                                      // The I/O track (FilePrimitives.cpp,
                                      // IoPrimitives.cpp, HttpPrimitives.cpp).
                                      "__srcOpen", "__srcStdin", "__srcLine", "__srcRest",
                                      "__srcClose", "__stdinLine",
                                      "__fileReadBytes", "__fileWriteBytes", "__fileStat",
                                      "__fileList", "__fileMkdirs", "__fileMove",
                                      "__fileCopy", "__fileRemoveTree",
                                      "__bytesFromSeq", "__bytesFromString", "__bytesLength",
                                      "__bytesAt", "__bytesSlice", "__bytesConcat",
                                      "__bytesEquals", "__bytesHash", "__bytesToList",
                                      "__bytesDecode", "__bytesShow",
                                      "__ioEnv", "__ioProps", "__ioExit", "__ioStderr",
                                      "__procRunInherit", "__procCapture", "__procSpawn",
                                      "__procWait", "__procKill",
                                      "__tcpConnect", "__tcpListen", "__tcpAccept",
                                      "__sockName", "__peerName",
                                      "__fdReadLine", "__fdRead", "__fdReadBytes",
                                      "__fdReadAll", "__fdWrite", "__fdSetTimeout",
                                      "__fdClose", "__tlsConnect",
                                      "__udpBind", "__udpSend", "__udpReceive",
                                      "__httpRequest", "__httpReadRequest",
                                      "__httpWriteResponse", "__httpParseQuery",
                                      "__urlEncode"};
        for (unsigned n = 2; n <= kMaxTupleArity; ++n) v.push_back("Tuple" + std::to_string(n));
        return v;
    }();
    return names;
}

const std::vector<BuiltinByNameSignature>& builtinByNameSignatures() {
    // `Future(body)` takes its body by name (D47): the argument is wrapped in a
    // thunk the future forces on its own thread.
    static const std::vector<BuiltinByNameSignature> sigs = {{"Future", {0x1u}}};
    return sigs;
}

} // namespace protoScala
