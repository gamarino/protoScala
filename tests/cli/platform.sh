# Sourced (not run) by the CLI checks that build or load native libraries, so
# that each of them names the platform's differences the same way.
#
#   PS_WINDOWS     1 under Git for Windows' bash (MSYS), 0 elsewhere
#   SO             the suffix of a loadable library: dll on Windows, so elsewhere
#   PATHSEP        the separator of a list of paths: ';' on Windows, ':' elsewhere
#   MAKE_CMD       the command that runs the build file protoscalac --emit-make
#                  writes: nmake on Windows (from a Developer environment), make
#                  elsewhere (an array)
#   build_module_dir <dir>
#                  runs MAKE_CMD in <dir>
#   build_plain_library <out-library> <source.cpp>
#                  compiles a one-file shared library with the host's compiler;
#                  its functions must be marked with PS_EXPORT
#   PS_EXPORT      what marks an exported function in that source
#   library_dependencies <library>
#                  prints what a library loads (ldd, otool -L or dumpbin)
#
# Options of the Microsoft tools are spelled with '-' rather than '/', because
# MSYS rewrites an argument that starts with '/' into a Windows path.

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) PS_WINDOWS=1; SO=dll; PATHSEP=';' ;;
    *)                    PS_WINDOWS=0; SO=so;  PATHSEP=':' ;;
esac

if [[ $PS_WINDOWS -eq 1 ]]; then
    PS_EXPORT='extern "C" __declspec(dllexport)'
else
    PS_EXPORT='extern "C"'
fi

if [[ $PS_WINDOWS -eq 1 ]]; then
    MAKE_CMD=(nmake -nologo)
else
    MAKE_CMD=(make)
fi

build_module_dir() {
    ( cd "$1" && "${MAKE_CMD[@]}" )
}

build_plain_library() {
    local out="$1" src="$2"
    if [[ $PS_WINDOWS -eq 1 ]]; then
        local dir
        dir=$(dirname "$out")
        ( cd "$dir" && cl -nologo -LD "$src" "-Fe:$out" ) >/dev/null
    else
        c++ -shared -fPIC -o "$out" "$src"
    fi
}

library_dependencies() {
    if [[ $PS_WINDOWS -eq 1 ]]; then
        dumpbin -nologo -dependents "$1"
    elif command -v ldd >/dev/null 2>&1; then
        ldd "$1"
    else
        otool -L "$1"
    fi
}
