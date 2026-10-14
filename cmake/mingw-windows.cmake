# Cross-compile the PC port to 64-bit Windows from a Linux host.
#
# Uses a Linux-hosted clang targeting x86_64-w64-mingw32, links with lld, and
# reads a MinGW-w64 sysroot (headers + import libs + static archives). The
# result is a statically linked PE32+ whose only runtime DLL dependencies are
# KERNEL32 and the Universal CRT (api-ms-win-crt-*) that ship with Windows 10+,
# so the built bfbb.exe is portable across machines with no bundled CRT.
#
# Required environment (all mandatory):
#   BFBB_MINGW_SYSROOT  root of the MinGW-w64 sysroot, e.g. .../mingw64
#                       Must contain x86_64-w64-mingw32/include, .../lib,
#                       include/c++/<ver>, lib/gcc/x86_64-w64-mingw32/<ver>.
#   BFBB_MINGW_LLD      path to a Linux-hosted ld.lld (from the lld package).
#   BFBB_MINGW_RC       (optional) path to a Linux-hosted llvm-rc, for the icon.
#
# Example:
#   export BFBB_MINGW_SYSROOT=/tmp/opencode/mingw/mingw64
#   export BFBB_MINGW_LLD=/usr/lib/llvm-19/bin/ld.lld
#   export BFBB_MINGW_RC=/usr/bin/llvm-rc-19
#   cmake -S . -B build-win64 -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-windows.cmake \
#         -DCMAKE_BUILD_TYPE=Release -DBFBB_RENDER_BACKENDS=D3D9\;D3D11\;GL3 \
#         -DBFBB_SDL=vendored -DBFBB_BUILD_CONFIGURATOR=OFF

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT DEFINED ENV{BFBB_MINGW_SYSROOT})
  message(FATAL_ERROR "mingw-windows toolchain: set BFBB_MINGW_SYSROOT to the MinGW-w64 sysroot")
endif()
if(NOT DEFINED ENV{BFBB_MINGW_LLD})
  message(FATAL_ERROR "mingw-windows toolchain: set BFBB_MINGW_LLD to a Linux-hosted ld.lld")
endif()

set(BFBB_MINGW_SYSROOT "$ENV{BFBB_MINGW_SYSROOT}")
set(BFBB_MINGW_LLD "$ENV{BFBB_MINGW_LLD}")

# Locate the per-version C++ include dir inside the sysroot so the flags below
# are not pinned to a single toolchain release. GCC's own intrinsics dir
# (lib/gcc/.../include) is deliberately NOT added: those headers call GCC-only
# builtins clang does not implement, and clang ships its own intrinsics which it
# finds via its resource dir automatically.
file(GLOB _bfbb_cxx_inc "${BFBB_MINGW_SYSROOT}/include/c++/*")
file(GLOB _bfbb_cxx_tgt "${BFBB_MINGW_SYSROOT}/include/c++/*/x86_64-w64-mingw32")
file(GLOB _bfbb_gcc_lib "${BFBB_MINGW_SYSROOT}/lib/gcc/x86_64-w64-mingw32/*")

list(SORT _bfbb_cxx_inc)
list(SORT _bfbb_cxx_tgt)
list(SORT _bfbb_gcc_lib)
list(GET _bfbb_cxx_inc 0 _bfbb_cxx_inc)
list(GET _bfbb_cxx_tgt 0 _bfbb_cxx_tgt)
list(GET _bfbb_gcc_lib 0 _bfbb_gcc_lib)

# The C headers (windows.h, stdio.h, ...) live in x86_64-w64-mingw32/include.
set(_bfbb_mingw_inc "${BFBB_MINGW_SYSROOT}/x86_64-w64-mingw32/include")

# Order matters for #include_next in the C++ headers: C++ wrapper dirs first,
# then the C headers last so #include_next <stdlib.h> finds them.
set(_bfbb_isystem
  "-isystem ${_bfbb_cxx_inc}"
  "-isystem ${_bfbb_cxx_tgt}"
  "-isystem ${_bfbb_mingw_inc}"
)
string(REPLACE ";" " " _bfbb_isystem "${_bfbb_isystem}")

# Compiler: clang/clang++ cross-targeting mingw. CMAKE_<LANG>_COMPILER_TARGET
# makes clang emit the right target for every subproject (SDL, librw, game).
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER_TARGET x86_64-w64-mingw32)
set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-mingw32)

# Resource compiler for the icon. Optional: without it CMake omits the .rc and
# the build still succeeds (just no icon).
if(DEFINED ENV{BFBB_MINGW_RC} AND EXISTS "$ENV{BFBB_MINGW_RC}")
  set(CMAKE_RC_COMPILER "$ENV{BFBB_MINGW_RC}")
  set(CMAKE_RC_COMPILER_TARGET x86_64-w64-mingw32)
endif()

# Static link: pull in libstdc++, libgcc, winpthread and the mingw startup objs
# as archives so the exe carries no C++ runtime DLL. lld is the linker.
#
# Search paths and -static/-fuse-ld go in the linker FLAGS (order-independent,
# placed before the objects). The -l archives go in STANDARD_LIBRARIES, which
# CMake appends AFTER the objects -- placing them before the objects makes lld
# see libmingw32's startup alongside the CRT's and report a duplicate `main`.
set(_bfbb_link_paths
  "-L${BFBB_MINGW_SYSROOT}/x86_64-w64-mingw32/lib"
  "-L${BFBB_MINGW_SYSROOT}/lib"
  "-L${_bfbb_gcc_lib}"
)
string(REPLACE ";" " " _bfbb_link_paths "${_bfbb_link_paths}")

# Order matters for lld's single left-to-right pass: libgcc_eh's emutls.o
# references pthread_*, so libwinpthread must come AFTER -lgcc/-lgcc_eh.
set(_bfbb_link_libs "-lstdc++ -lmingwex -lmingw32 -lgcc -lgcc_eh -lwinpthread")

set(CMAKE_C_FLAGS_INIT "${_bfbb_isystem}")
set(CMAKE_CXX_FLAGS_INIT "${_bfbb_isystem}")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=${BFBB_MINGW_LLD} -static ${_bfbb_link_paths}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-fuse-ld=${BFBB_MINGW_LLD} ${_bfbb_link_paths}")
set(CMAKE_C_STANDARD_LIBRARIES_INIT "${_bfbb_link_libs}")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT "${_bfbb_link_libs}")

# find_library / find_path must resolve against the mingw sysroot, not the host.
set(CMAKE_FIND_ROOT_PATH "${BFBB_MINGW_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Do not build host-runnable tools (selftests) for a cross target.
set(BFBB_HOST_TOOLS OFF CACHE BOOL "" FORCE)
