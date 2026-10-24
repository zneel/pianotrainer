# Builds mda Piano (the VST 3 SDK's mda-vst3 sample, MIT) as Saved/TestPlugins/mdaPiano.vst3, a real VST 3 instrument
# to test the app's plug-in host with (tools/run_tests.ps1 -Only vst3_*). Windows, Visual Studio with the C++ tools.
#
# -Sdk: a folder with the SDK's parts cloned side by side (v3.8.1_build_84, from github.com/steinbergmedia):
#   git clone --depth 1 --branch v3.8.1_build_84 https://github.com/steinbergmedia/vst3_pluginterfaces.git
#   git clone --depth 1 --branch v3.8.1_build_84 https://github.com/steinbergmedia/vst3_base.git
#   git clone --depth 1 --branch v3.8.1_build_84 https://github.com/steinbergmedia/vst3_public_sdk.git
param([Parameter(Mandatory = $true)][string]$Sdk)
$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $PSScriptRoot
$Work = Join-Path $Root "Saved\TestPlugins\build"
$Bundle = Join-Path $Root "Saved\TestPlugins\mdaPiano.vst3"

# the SDK includes "pluginterfaces/...", "base/..." and "public.sdk/..." from one root
$Inc = Join-Path $Work "sdk"
Remove-Item $Work -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Inc | Out-Null
Copy-Item (Join-Path $Sdk "vst3_pluginterfaces") (Join-Path $Inc "pluginterfaces") -Recurse
Copy-Item (Join-Path $Sdk "vst3_base") (Join-Path $Inc "base") -Recurse
Copy-Item (Join-Path $Sdk "vst3_public_sdk") (Join-Path $Inc "public.sdk") -Recurse
$Mda = Join-Path $Inc "public.sdk\samples\vst\mda-vst3\source"
$Pub = Join-Path $Inc "public.sdk\source"

# The sample drops a note that falls exactly at the end of one of its 16-frame slices: it is queued in the slice that
# ends there and never played (round score times at 48 kHz land there often: 9 of 19 notes of the Ode). It belongs
# to the next slice.
$Base = Join-Path $Mda "mdaBaseProcessor.cpp"
$Text = Get-Content $Base -Raw
if (-not $Text.Contains("if (event.sampleOffset <= data.numSamples)")) { throw "mdaBaseProcessor.cpp changed: check the slice fix" }
Set-Content $Base ($Text.Replace("if (event.sampleOffset <= data.numSamples)", "if (event.sampleOffset < data.numSamples)")) -NoNewline

$Sources = @(
    "$PSScriptRoot\test_vst3\mdapiano_factory.cpp",
    "$Mda\mdaBaseProcessor.cpp", "$Mda\mdaBaseController.cpp", "$Mda\mdaParameter.cpp", "$Mda\mdaPianoProcessor.cpp", "$Mda\mdaPianoController.cpp",
    "$Pub\main\dllmain.cpp", "$Pub\main\pluginfactory.cpp", "$Pub\main\moduleinit.cpp",
    "$Pub\vst\vstaudioeffect.cpp", "$Pub\vst\vstcomponent.cpp", "$Pub\vst\vstcomponentbase.cpp", "$Pub\vst\vsteditcontroller.cpp",
    "$Pub\vst\vstparameters.cpp", "$Pub\vst\vstbus.cpp", "$Pub\vst\vstinitiids.cpp", "$Pub\vst\utility\vst2persistence.cpp",
    "$Pub\common\commoniids.cpp", "$Pub\common\pluginview.cpp",
    "$Inc\base\source\baseiids.cpp", "$Inc\base\source\fbuffer.cpp", "$Inc\base\source\fdebug.cpp", "$Inc\base\source\fobject.cpp",
    "$Inc\base\source\fstreamer.cpp", "$Inc\base\source\fstring.cpp", "$Inc\base\source\timer.cpp", "$Inc\base\source\updatehandler.cpp",
    "$Inc\base\thread\source\fcondition.cpp", "$Inc\base\thread\source\flock.cpp",
    "$Inc\pluginterfaces\base\conststringtable.cpp", "$Inc\pluginterfaces\base\coreiids.cpp", "$Inc\pluginterfaces\base\funknown.cpp",
    "$Inc\pluginterfaces\base\ustring.cpp"
)

$Vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$VcVars = Join-Path $Vs "VC\Auxiliary\Build\vcvars64.bat"
$Binary = Join-Path $Work "mdaPiano.vst3"
$Cmd = "`"$VcVars`" >nul && cd /d `"$Work`" && cl /nologo /std:c++17 /O2 /EHsc /MD /LD /DRELEASE=1 /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS " +
    "/I`"$Inc`" /I`"$Mda`" /I`"$PSScriptRoot\test_vst3`" " + (($Sources | ForEach-Object { "`"$_`"" }) -join " ") + " /Fe:`"$Binary`" /link user32.lib ole32.lib shell32.lib"
cmd /c $Cmd
if ($LASTEXITCODE -ne 0) { throw "build failed" }

# a VST 3 bundle: <name>.vst3/Contents/x86_64-win/<name>.vst3
$Arch = Join-Path $Bundle "Contents\x86_64-win"
Remove-Item $Bundle -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $Arch | Out-Null
Copy-Item $Binary $Arch
Write-Output "built $Bundle"
