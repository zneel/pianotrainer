#!/usr/bin/env bash
# macOS counterpart of build_win64.bat: builds universal (arm64 + x86_64) binaries with Xcode's clang.
#   Verovio 6.3.0 -> Plugins/PianoKit/ThirdParty/Verovio/Mac/libverovio.dylib (C API only; LGPL, stays a dylib)
#   lunasvg 3.5.0 + plutovg -> Plugins/PianoKit/ThirdParty/LunaSVG/Mac/liblunasvg.a
#   sfizz 1.2.3 + its bundled deps -> Plugins/PianoKit/ThirdParty/Sfizz/Mac/libsfizz.dylib (C API, BSD-2)
# Needs: Xcode command line tools and git (to apply patches/). No CMake, no downloads.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
VRV="$ROOT/src/verovio-version-6.3.0"
LSVG="$ROOT/src/lunasvg-3.5.0"
OBJ="$ROOT/build/mac"
OUT="$ROOT/../Plugins/PianoKit/ThirdParty"
JOBS="$(sysctl -n hw.ncpu)"
ARCH="-arch arm64 -arch x86_64 -mmacosx-version-min=13.0"

mkdir -p "$OBJ/verovio" "$OBJ/lunasvg" "$OUT/Verovio/Mac" "$OUT/Verovio/include" "$OUT/LunaSVG/Mac" "$OUT/LunaSVG/include"

# ---------------- Verovio ----------------
# Patch: accidentals implied by the key signature in Humdrum (**kern) files must sound (patches/ folder).
PATCH="$ROOT/patches/verovio-6.3.0-humdrum-gestural-accidentals.patch"
git -C "$VRV" apply --check "$PATCH" 2>/dev/null && git -C "$VRV" apply "$PATCH"
git -C "$VRV" apply -R --check "$PATCH" || { echo "[verovio] patch could not be applied"; exit 1; }
VRV_FLAGS="$ARCH -O2 -std=c++20 -fPIC -fvisibility=default -DNO_DARMS_SUPPORT -DNO_RUNTIME -DRESOURCE_DIR=\"\" -w"
for d in include include/crc include/midi include/hum include/json include/pugi include/tuning-library include/zip include/vrv libmei/dist libmei/addons; do
  VRV_FLAGS="$VRV_FLAGS -I$VRV/$d"
done
export VRV_FLAGS OBJ
echo "[verovio] compiling with $JOBS jobs..."
{ ls "$VRV"/src/*.cpp "$VRV"/libmei/dist/*.cpp "$VRV"/libmei/addons/*.cpp "$VRV"/src/hum/*.cpp "$VRV"/src/crc/*.cpp "$VRV"/src/midi/*.cpp;
  echo "$VRV/src/json/jsonxx.cc"; echo "$VRV/src/pugi/pugixml.cpp"; echo "$VRV/tools/c_wrapper.cpp"; } |
  xargs -P "$JOBS" -I{} sh -c 'f="{}"; o="$OBJ/verovio/$(basename "${f%.*}").o"; [ "$o" -nt "$f" ] || clang++ $VRV_FLAGS -c "$f" -o "$o"'

echo "[verovio] linking..."
EXPORTS="$OBJ/verovio_exports.txt"
grep -oE '\b(vrvToolkit_[A-Za-z]+|enableLog[A-Za-z]*)\(' "$VRV/tools/c_wrapper.h" | tr -d '(' | sort -u | sed 's/^/_/' > "$EXPORTS"
clang++ $ARCH -dynamiclib -o "$OUT/Verovio/Mac/libverovio.dylib" "$OBJ"/verovio/*.o \
  -install_name @rpath/libverovio.dylib -exported_symbols_list "$EXPORTS"
cp "$VRV/tools/c_wrapper.h" "$OUT/Verovio/include/verovio_c.h"
rsync -a "$VRV/data/" "$OUT/Verovio/data/"
# music font (OFL) for SMuFL glyphs Verovio writes as <text> (tempo marks etc.)
mkdir -p "$OUT/Verovio/fonts" && cp "$VRV/fonts/Leipzig/Leipzig.ttf" "$OUT/Verovio/fonts/Leipzig.ttf"
cp "$VRV/COPYING.LESSER" "$OUT/Verovio/LICENSE.LGPL3.txt"
cp "$VRV/COPYING" "$OUT/Verovio/LICENSE.GPL3.txt"

# ---------------- lunasvg + plutovg ----------------
echo "[lunasvg] compiling..."
LS_DEFS="-DLUNASVG_BUILD -DLUNASVG_BUILD_STATIC -DPLUTOVG_BUILD -DPLUTOVG_BUILD_STATIC"
LS_INC="-I$LSVG/include -I$LSVG/source -I$LSVG/plutovg/include -I$LSVG/plutovg/source"
for f in "$LSVG"/plutovg/source/*.c; do
  clang $ARCH -O2 -std=c11 -fPIC $LS_DEFS $LS_INC -c "$f" -o "$OBJ/lunasvg/$(basename "${f%.*}").o" &
done
for f in "$LSVG"/source/*.cpp; do
  clang++ $ARCH -O2 -std=c++17 -fPIC $LS_DEFS $LS_INC -c "$f" -o "$OBJ/lunasvg/$(basename "${f%.*}").o" &
done
wait
rm -f "$OUT/LunaSVG/Mac/liblunasvg.a"
libtool -static -o "$OUT/LunaSVG/Mac/liblunasvg.a" "$OBJ"/lunasvg/*.o
cp "$LSVG/include/lunasvg.h" "$OUT/LunaSVG/include/lunasvg.h"
cp "$LSVG/LICENSE" "$OUT/LunaSVG/LICENSE.txt"

# ---------------- sfizz ----------------
# Mirrors src/CMakeLists.txt + cmake/SfizzDeps.cmake (libsndfile off: FLAC/WAV via dr_libs, WavPack, libaiff).
SFZ="$ROOT/src/sfizz-1.2.3"
sed -i '' 's/maxVoices { 256 }/maxVoices { 1024 }/' "$SFZ/src/sfizz/Config.h" # voice cap 256 -> 1024 (see README)
grep -q 'maxVoices { 1024 }' "$SFZ/src/sfizz/Config.h" || { echo "[sfizz] voice cap patch FAILED"; exit 1; }
mkdir -p "$OBJ/sfizz" "$OUT/Sfizz/Mac" "$OUT/Sfizz/include"
echo '#include <ghc/fs_std_impl.hpp>' > "$OBJ/sfizz/fs_std_impl.cpp"
SFZ_INC=""
for d in src src/sfizz src/sfizz/utility/spin_mutex src/sfizz/utility/bit_array external/abseil-cpp external/filesystem/include \
         external/atomic_queue/include external/simde external/jsl/include external/threadpool external/st_audiofile/src \
         external/st_audiofile/thirdparty/dr_libs external/st_audiofile/thirdparty/stb_vorbis external/st_audiofile/thirdparty/libaiff \
         external/st_audiofile/thirdparty/wavpack/include src/external/pugixml/src src/external/spline src/external/tunings/include \
         src/external/hiir src/external/kiss_fft external/cephes/src src/external/cpuid/src src/external/cpuid/platform/src; do
  SFZ_INC="$SFZ_INC -I$SFZ/$d"
done
SFZ_DEFS="-DNDEBUG -DGHC_FILESYSTEM_FWD -DSFIZZ_EXPORT_SYMBOLS -DENABLE_THREADS -DENABLE_DSD"
export SFZ SFZ_INC SFZ_DEFS ARCH OBJ
compile_sfizz() {
  f="$1"; rel="${f#$SFZ/}"; o="$OBJ/sfizz/$(echo "$rel" | tr '/+' '__').o"
  [ "$o" -nt "$f" ] && return 0
  case "$f" in
    *.c) clang $ARCH -O2 -std=c11 -fPIC $SFZ_DEFS $SFZ_INC -w -c "$f" -o "$o" ;;
    *AVX.cpp) clang++ $ARCH -O2 -std=c++17 -fPIC -fvisibility=hidden -ffast-math -Xarch_x86_64 -mavx $SFZ_DEFS $SFZ_INC -w -c "$f" -o "$o" ;;
    *) clang++ $ARCH -O2 -std=c++17 -fPIC -fvisibility=hidden -ffast-math $SFZ_DEFS $SFZ_INC -w -c "$f" -o "$o" ;;
  esac
}
export -f compile_sfizz
A="$SFZ/external/abseil-cpp/absl"; W="$SFZ/external/st_audiofile/thirdparty/wavpack/src"
echo "[sfizz] compiling with $JOBS jobs..."
{ ls "$SFZ"/src/sfizz/*.cpp "$SFZ"/src/sfizz/effects/*.cpp "$SFZ"/src/sfizz/effects/impl/*.cpp "$SFZ"/src/sfizz/modulations/*.cpp \
     "$SFZ"/src/sfizz/modulations/sources/*.cpp "$SFZ"/src/sfizz/parser/*.cpp "$SFZ"/src/sfizz/simd/*.cpp \
     "$SFZ"/src/sfizz/utility/c++17/*.cpp "$SFZ"/src/sfizz/utility/spin_mutex/*.cpp;
  for f in src/external/pugixml/src/pugixml.cpp src/external/spline/spline/spline.cpp src/external/tunings/src/Tunings.cpp \
           src/external/hiir/hiir/PolyphaseIir2Designer.cpp src/external/cpuid/src/cpuid/cpuinfo.cpp src/external/cpuid/src/cpuid/version.cpp \
           external/st_audiofile/src/st_audiofile.c external/st_audiofile/src/st_audiofile_common.c external/st_audiofile/src/st_audiofile_sndfile.c \
           external/st_audiofile/src/st_audiofile_libs.c external/st_audiofile/thirdparty/libaiff/libaiff.all.c src/external/kiss_fft/kiss_fft.c \
           src/external/kiss_fft/kiss_fftr.c external/cephes/src/chbevl.c external/cephes/src/i0.c; do echo "$SFZ/$f"; done
  for f in base/internal/raw_logging base/internal/throw_delegate numeric/int128 hash/internal/hash hash/internal/city \
           hash/internal/low_level_hash container/internal/raw_hash_set strings/ascii strings/charconv strings/match strings/numbers \
           strings/str_cat strings/str_replace strings/str_split strings/escaping strings/internal/memutil strings/internal/charconv_bigint \
           strings/internal/charconv_parse strings/internal/escaping strings/internal/ostringstream strings/internal/utf8; do echo "$A/$f.cc"; done
  for f in common_utils decorr_utils entropy_utils extra1 extra2 open_utils open_filename open_legacy open_raw pack pack_dns pack_floats \
           pack_utils read_words tags tag_utils unpack unpack_floats unpack_seek unpack_utils write_words pack_dsd unpack_dsd; do echo "$W/$f.c"; done
  echo "$OBJ/sfizz/fs_std_impl.cpp"; } | xargs -P "$JOBS" -I{} bash -c 'compile_sfizz "{}"'
echo "[sfizz] linking..."
clang++ $ARCH -dynamiclib -o "$OUT/Sfizz/Mac/libsfizz.dylib" "$OBJ"/sfizz/*.o -install_name @rpath/libsfizz.dylib -framework CoreFoundation
cp "$SFZ/src/sfizz.h" "$SFZ/src/sfizz_message.h" "$OUT/Sfizz/include/"
cp "$SFZ/LICENSE" "$OUT/Sfizz/LICENSE.txt"
cp "$SFZ/external/abseil-cpp/LICENSE" "$OUT/Sfizz/LICENSE.abseil.txt"

echo "[done] $OUT"
