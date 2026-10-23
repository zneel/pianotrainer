@echo off
rem Builds the third-party libraries for Win64 with the same MSVC toolchain as UE 5.8:
rem   Verovio 6.3.0 -> verovio.dll + verovio.lib (C API only, see verovio.def; LGPL so it stays a separate DLL)
rem   lunasvg 3.5.0 + plutovg -> lunasvg.lib (static, MIT)
rem   sfizz 1.2.3 + its bundled deps -> sfizz.dll (C API, BSD-2; a DLL keeps its abseil copy private)
rem Output goes to Plugins\PianoKit\ThirdParty. No CMake needed: plain cl/link with response files.
setlocal enabledelayedexpansion

set ROOT=%~dp0
set VRV=%ROOT%src\verovio-version-6.3.0
set LSVG=%ROOT%src\lunasvg-3.5.0
set OBJ=%ROOT%build\win64
set OUT=%ROOT%..\Plugins\PianoKit\ThirdParty

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if not defined VSPATH (echo No MSVC toolset found & exit /b 1)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

if not exist "%OBJ%\verovio" mkdir "%OBJ%\verovio"
if not exist "%OBJ%\lunasvg" mkdir "%OBJ%\lunasvg"
if not exist "%OUT%\Verovio\Win64" mkdir "%OUT%\Verovio\Win64"
if not exist "%OUT%\Verovio\include" mkdir "%OUT%\Verovio\include"
if not exist "%OUT%\LunaSVG\Win64" mkdir "%OUT%\LunaSVG\Win64"
if not exist "%OUT%\LunaSVG\include" mkdir "%OUT%\LunaSVG\include"
rem "build_win64.bat sfizz" rebuilds only sfizz
if /i "%~1"=="sfizz" goto :only_sfizz

rem ---------------- Verovio ----------------
rem Patch: accidentals implied by the key signature in Humdrum (**kern) files must sound (patches\ folder).
git -C "%VRV%" apply --check "%ROOT%patches\verovio-6.3.0-humdrum-gestural-accidentals.patch" 2>nul && git -C "%VRV%" apply "%ROOT%patches\verovio-6.3.0-humdrum-gestural-accidentals.patch"
git -C "%VRV%" apply -R --check "%ROOT%patches\verovio-6.3.0-humdrum-gestural-accidentals.patch" 2>nul || (echo [verovio] patch could not be applied & exit /b 1)
set RSP=%OBJ%\verovio.rsp
> "%RSP%" echo /nologo /c /MP /O2 /MD /EHsc /GR /std:c++20 /bigobj /utf-8 /W2 /wd4244 /wd4996
>>"%RSP%" echo /DNO_DARMS_SUPPORT /DNO_PAE_SUPPORT /DUSE_PAE_OLD_PARSER /DNO_RUNTIME /DRESOURCE_DIR=\"\" /D_CRT_SECURE_NO_WARNINGS
for %%d in (include include\crc include\midi include\hum include\json include\pugi include\tuning-library include\zip include\vrv include\win32 libmei\dist libmei\addons) do >>"%RSP%" echo /I"%VRV%\%%d"
>>"%RSP%" echo /Fo"%OBJ%\verovio\\"
for %%d in (src libmei\dist libmei\addons src\hum src\crc src\midi) do for %%f in ("%VRV%\%%d\*.cpp") do >>"%RSP%" echo "%%~f"
>>"%RSP%" echo "%VRV%\src\json\jsonxx.cc"
>>"%RSP%" echo "%VRV%\src\pugi\pugixml.cpp"
>>"%RSP%" echo "%VRV%\tools\c_wrapper.cpp"

echo [verovio] compiling...
cl @"%RSP%" > "%OBJ%\verovio_cl.log" 2>&1
if errorlevel 1 (echo [verovio] compile FAILED, see %OBJ%\verovio_cl.log & exit /b 1)

echo [verovio] linking...
link /nologo /DLL /OUT:"%OUT%\Verovio\Win64\verovio.dll" /IMPLIB:"%OUT%\Verovio\Win64\verovio.lib" /DEF:"%ROOT%verovio.def" "%OBJ%\verovio\*.obj" > "%OBJ%\verovio_link.log" 2>&1
if errorlevel 1 (echo [verovio] link FAILED, see %OBJ%\verovio_link.log & exit /b 1)
copy /y "%VRV%\tools\c_wrapper.h" "%OUT%\Verovio\include\verovio_c.h" >nul
xcopy /y /e /i /q "%VRV%\data" "%OUT%\Verovio\data" >nul
if not exist "%OUT%\Verovio\fonts" mkdir "%OUT%\Verovio\fonts"
rem music font (OFL) for SMuFL glyphs Verovio writes as <text> (tempo marks etc.); lunasvg needs it as a font file
copy /y "%VRV%\fonts\Leipzig\Leipzig.ttf" "%OUT%\Verovio\fonts\Leipzig.ttf" >nul
copy /y "%VRV%\COPYING.LESSER" "%OUT%\Verovio\LICENSE.LGPL3.txt" >nul
copy /y "%VRV%\COPYING" "%OUT%\Verovio\LICENSE.GPL3.txt" >nul

rem ---------------- lunasvg + plutovg ----------------
set RSP=%OBJ%\lunasvg.rsp
> "%RSP%" echo /nologo /c /MP /O2 /MD /EHsc /utf-8 /W2 /DLUNASVG_BUILD /DLUNASVG_BUILD_STATIC /DPLUTOVG_BUILD /DPLUTOVG_BUILD_STATIC /D_CRT_SECURE_NO_WARNINGS
>>"%RSP%" echo /I"%LSVG%\include" /I"%LSVG%\source" /I"%LSVG%\plutovg\include" /I"%LSVG%\plutovg\source"
>>"%RSP%" echo /Fo"%OBJ%\lunasvg\\"
for %%f in ("%LSVG%\plutovg\source\*.c") do >>"%RSP%" echo "%%~f"
echo [lunasvg] compiling...
cl @"%RSP%" /std:c11 > "%OBJ%\lunasvg_cl.log" 2>&1
if errorlevel 1 (echo [lunasvg] plutovg compile FAILED, see %OBJ%\lunasvg_cl.log & exit /b 1)
set RSP2=%OBJ%\lunasvg_cpp.rsp
copy /y "%RSP%" "%RSP2%" >nul
findstr /v /c:".c\"" "%RSP%" > "%RSP2%"
for %%f in ("%LSVG%\source\*.cpp") do >>"%RSP2%" echo "%%~f"
cl @"%RSP2%" /std:c++17 >> "%OBJ%\lunasvg_cl.log" 2>&1
if errorlevel 1 (echo [lunasvg] compile FAILED, see %OBJ%\lunasvg_cl.log & exit /b 1)
lib /nologo /OUT:"%OUT%\LunaSVG\Win64\lunasvg.lib" "%OBJ%\lunasvg\*.obj" || exit /b 1
copy /y "%LSVG%\include\lunasvg.h" "%OUT%\LunaSVG\include\lunasvg.h" >nul
copy /y "%LSVG%\LICENSE" "%OUT%\LunaSVG\LICENSE.txt" >nul

:only_sfizz
rem ---------------- sfizz ----------------
rem Mirrors src/CMakeLists.txt + cmake/SfizzDeps.cmake (libsndfile off: FLAC/WAV via dr_libs, WavPack, libaiff).
rem Each group compiles into its own object folder because several sources share a file name.
set SFZ=%ROOT%src\sfizz-1.2.3
set SOBJ=%OBJ%\sfizz
if not exist "%OUT%\Sfizz\Win64" mkdir "%OUT%\Sfizz\Win64"
if not exist "%OUT%\Sfizz\include" mkdir "%OUT%\Sfizz\include"
rem voice cap 256 -> 1024 (all uses of config::maxVoices are capacities, see README)
powershell -NoProfile -Command "$f='%SFZ%\src\sfizz\Config.h'; (Get-Content $f) -replace 'maxVoices \{ 256 \}','maxVoices { 1024 }' | Set-Content -Encoding ascii $f" || exit /b 1
findstr /c:"maxVoices { 1024 }" "%SFZ%\src\sfizz\Config.h" >nul || (echo [sfizz] voice cap patch FAILED & exit /b 1)
if not exist "%SOBJ%" mkdir "%SOBJ%"
> "%SOBJ%\fs_std_impl.cpp" echo #include ^<ghc/fs_std_impl.hpp^>

set SRSP=%SOBJ%\common.rsp
> "%SRSP%" echo /nologo /c /MP /O2 /MD /fp:fast /utf-8 /W1 /Zc:__cplusplus
>>"%SRSP%" echo /DNDEBUG /D_USE_MATH_DEFINES /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /DGHC_FILESYSTEM_FWD /DSFIZZ_EXPORT_SYMBOLS /DENABLE_THREADS /DENABLE_DSD
for %%d in (src src\sfizz src\sfizz\utility\spin_mutex src\sfizz\utility\bit_array external\abseil-cpp external\filesystem\include external\atomic_queue\include external\simde external\jsl\include external\threadpool external\st_audiofile\src external\st_audiofile\thirdparty\dr_libs external\st_audiofile\thirdparty\stb_vorbis external\st_audiofile\thirdparty\libaiff external\st_audiofile\thirdparty\wavpack\include src\external\pugixml\src src\external\spline src\external\tunings\include src\external\hiir src\external\kiss_fft external\cephes\src src\external\cpuid\src src\external\cpuid\platform\src) do >>"%SRSP%" echo /I"%SFZ%\%%d"

echo [sfizz] compiling...
> "%OBJ%\sfizz_cl.log" echo sfizz build
pushd "%SFZ%"
set CXX=/std:c++17 /EHsc /GR
call :sfzgroup core   "%CXX%" "src\sfizz\*.cpp" || goto :sfzfail
call :sfzgroup fx     "%CXX%" "src\sfizz\effects\*.cpp" || goto :sfzfail
call :sfzgroup fximpl "%CXX%" "src\sfizz\effects\impl\ResonantString.cpp src\sfizz\effects\impl\ResonantStringSSE.cpp src\sfizz\effects\impl\ResonantArray.cpp src\sfizz\effects\impl\ResonantArraySSE.cpp" || goto :sfzfail
call :sfzgroup avx    "%CXX% /arch:AVX" "src\sfizz\effects\impl\ResonantStringAVX.cpp src\sfizz\effects\impl\ResonantArrayAVX.cpp src\sfizz\simd\HelpersAVX.cpp" || goto :sfzfail
call :sfzgroup simd   "%CXX%" "src\sfizz\simd\HelpersSSE.cpp src\sfizz\simd\HelpersNEON.cpp" || goto :sfzfail
call :sfzgroup mod    "%CXX%" "src\sfizz\modulations\*.cpp" || goto :sfzfail
call :sfzgroup modsrc "%CXX%" "src\sfizz\modulations\sources\*.cpp" || goto :sfzfail
call :sfzgroup parser "%CXX%" "src\sfizz\parser\*.cpp" || goto :sfzfail
call :sfzgroup util   "%CXX%" "src\sfizz\utility\c++17\*.cpp src\sfizz\utility\spin_mutex\*.cpp" || goto :sfzfail
call :sfzgroup ext    "%CXX%" "src\external\pugixml\src\pugixml.cpp src\external\spline\spline\spline.cpp src\external\tunings\src\Tunings.cpp src\external\hiir\hiir\PolyphaseIir2Designer.cpp src\external\cpuid\src\cpuid\cpuinfo.cpp src\external\cpuid\src\cpuid\version.cpp %SOBJ%\fs_std_impl.cpp" || goto :sfzfail
set A=external\abseil-cpp\absl
call :sfzgroup absl   "%CXX%" "%A%\base\internal\raw_logging.cc %A%\base\internal\throw_delegate.cc %A%\numeric\int128.cc %A%\hash\internal\hash.cc %A%\hash\internal\city.cc %A%\hash\internal\low_level_hash.cc %A%\container\internal\raw_hash_set.cc %A%\strings\ascii.cc %A%\strings\charconv.cc %A%\strings\match.cc %A%\strings\numbers.cc %A%\strings\str_cat.cc %A%\strings\str_replace.cc %A%\strings\str_split.cc %A%\strings\escaping.cc %A%\strings\internal\memutil.cc %A%\strings\internal\charconv_bigint.cc %A%\strings\internal\charconv_parse.cc %A%\strings\internal\escaping.cc %A%\strings\internal\ostringstream.cc %A%\strings\internal\utf8.cc" || goto :sfzfail
call :sfzgroup audio  "/std:c11" "external\st_audiofile\src\st_audiofile.c external\st_audiofile\src\st_audiofile_common.c external\st_audiofile\src\st_audiofile_sndfile.c external\st_audiofile\src\st_audiofile_libs.c external\st_audiofile\thirdparty\libaiff\libaiff.all.c src\external\kiss_fft\kiss_fft.c src\external\kiss_fft\kiss_fftr.c external\cephes\src\chbevl.c external\cephes\src\i0.c" || goto :sfzfail
set W=external\st_audiofile\thirdparty\wavpack\src
call :sfzgroup wavpack "/std:c11" "%W%\common_utils.c %W%\decorr_utils.c %W%\entropy_utils.c %W%\extra1.c %W%\extra2.c %W%\open_utils.c %W%\open_filename.c %W%\open_legacy.c %W%\open_raw.c %W%\pack.c %W%\pack_dns.c %W%\pack_floats.c %W%\pack_utils.c %W%\read_words.c %W%\tags.c %W%\tag_utils.c %W%\unpack.c %W%\unpack_floats.c %W%\unpack_seek.c %W%\unpack_utils.c %W%\write_words.c %W%\pack_dsd.c %W%\unpack_dsd.c" || goto :sfzfail
popd

echo [sfizz] linking...
link /nologo /DLL /OUT:"%OUT%\Sfizz\Win64\sfizz.dll" /IMPLIB:"%OUT%\Sfizz\Win64\sfizz.lib" "%SOBJ%\core\*.obj" "%SOBJ%\fx\*.obj" "%SOBJ%\fximpl\*.obj" "%SOBJ%\avx\*.obj" "%SOBJ%\simd\*.obj" "%SOBJ%\mod\*.obj" "%SOBJ%\modsrc\*.obj" "%SOBJ%\parser\*.obj" "%SOBJ%\util\*.obj" "%SOBJ%\ext\*.obj" "%SOBJ%\absl\*.obj" "%SOBJ%\audio\*.obj" "%SOBJ%\wavpack\*.obj" > "%OBJ%\sfizz_link.log" 2>&1
if errorlevel 1 (echo [sfizz] link FAILED, see %OBJ%\sfizz_link.log & exit /b 1)
copy /y "%SFZ%\src\sfizz.h" "%OUT%\Sfizz\include\sfizz.h" >nul
copy /y "%SFZ%\src\sfizz_message.h" "%OUT%\Sfizz\include\sfizz_message.h" >nul
copy /y "%SFZ%\LICENSE" "%OUT%\Sfizz\LICENSE.txt" >nul
copy /y "%SFZ%\external\abseil-cpp\LICENSE" "%OUT%\Sfizz\LICENSE.abseil.txt" >nul

echo [done] %OUT%
exit /b 0

:sfzfail
popd
echo [sfizz] compile FAILED, see %OBJ%\sfizz_cl.log
exit /b 1

rem :sfzgroup <group> "<extra flags>" "<files or globs, relative to the sfizz folder>"
:sfzgroup
if not exist "%SOBJ%\%1" mkdir "%SOBJ%\%1"
set GRSP=%SOBJ%\%1.rsp
> "%GRSP%" echo /Fo"%SOBJ%\%1\\"
for %%f in (%~3) do >>"%GRSP%" echo "%%~ff"
>>"%OBJ%\sfizz_cl.log" echo ---- %1
cl @"%SRSP%" %~2 @"%GRSP%" >> "%OBJ%\sfizz_cl.log" 2>&1
exit /b %errorlevel%
