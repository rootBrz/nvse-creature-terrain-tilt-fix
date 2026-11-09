#!/usr/bin/env bash
set -euo pipefail

root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cxx="${CXX:-clang++}"
mingw="${MINGW_ROOT:-/usr/i686-w64-mingw32}"
gxx="${MINGW_GXX:-i686-w64-mingw32-g++}"
gcc_lib="${MINGW_GCC_LIB:-$(dirname "$("${gxx}" -print-libgcc-file-name)")}"

# Ask MinGW g++ where its libstdc++ headers are: the layout differs between
# distributions (Arch, Debian/Ubuntu).
cxx_includes=()
while read -r dir; do
  cxx_includes+=(-isystem "${dir}")
done < <("${gxx}" -E -x c++ -v - </dev/null 2>&1 | sed -n 's/^ \(.*\/c++.*\)$/\1/p')

mkdir -p "${root}/build"

"${cxx}" \
  --target=i686-w64-windows-gnu \
  --sysroot="${mingw}" \
  -std=c++20 \
  -fasm-blocks \
  -fms-extensions \
  -O2 \
  -Wno-invalid-constexpr \
  -fno-exceptions \
  -fno-rtti \
  -nostdlib \
  -shared \
  "${cxx_includes[@]}" \
  -I"${mingw}/include" \
  -I"${root}" \
  -L"${gcc_lib}" \
  -L"${mingw}/lib" \
  -include "${root}/compat.hpp" \
  "${root}/main.cpp" \
  "${root}/shared/SafeWrite/SafeWrite.cpp" \
  -Wl,-e,_DllMain@12 \
  -Wl,--start-group -lkernel32 -lmsvcrt -lmingwex -lgcc -luser32 -Wl,--end-group \
  -o "${root}/build/CreatureTerrainTiltFix.dll"
