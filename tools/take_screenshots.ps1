# Takes the README's screenshots (docs/screenshots) with the editor build: the menu pages, the practice screen while a
# Chopin étude plays, and Settings. Quiet (-quiet): nothing comes out of the speakers.
# usage: powershell -Command "& tools/take_screenshots.ps1 [-Sfz <an .sfz to show in Settings>]"
param([string]$Sfz = "", [string[]]$Only = @())
$Root = Split-Path -Parent $PSScriptRoot
$Out = Join-Path $Root "docs\screenshots"
$Logs = Join-Path $Root "Saved\PianoLogs"
New-Item -ItemType Directory -Force $Out | Out-Null
$Exe = "D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe"
$Etude = Join-Path $Root "Scores\Etudes\Chopin\010-1b-Sm-003.krn"
$Diary = Join-Path $Root "Saved\PianoLogs\tests\diary_test.json"
$Instrument = if ($Sfz) { @("-instrument=`"$Sfz`"") } else { @() }

# name, file the app writes, arguments
$Shots = @(
    @("home", "ui.png", @("-menupage=home", "-uishot=6", "-diary=`"$Diary`"")),
    @("etudes", "ui.png", @("-menupage=etudes", "-uishot=6")),
    @("diary", "ui.png", @("-menupage=diary", "-uishot=6", "-diary=`"$Diary`"")),
    @("settings", "ui.png", (@("-menupage=settings", "-uishot=8") + $Instrument)),
    @("practice", "shot.png", (@("-autotest", "-listen", "-wait=0", "-shot=21000", "-stop=23000", "-metronome=1", "-sidebartab=practice", "-score=`"$Etude`"") + $Instrument)),
    @("plan", "shot.png", (@("-autotest", "-listen", "-wait=0", "-shot=9000", "-stop=11000", "-sidebartab=plan", "-score=`"$Etude`"") + $Instrument))
)
foreach ($Shot in $Shots) {
    if ($Only.Count -gt 0 -and $Only -notcontains $Shot[0]) { continue }
    $File = Join-Path $Logs $Shot[1]
    Remove-Item $File -ErrorAction SilentlyContinue
    $A = @("`"$Root\PianoTrainer.uproject`"", "-game", "-windowed", "-resx=1600", "-resy=900", "-unattended", "-nosplash", "-quiet",
        "-log=shot_$($Shot[0]).log") + $Shot[2]
    $P = Start-Process -FilePath $Exe -ArgumentList $A -PassThru
    $P.WaitForExit(300000) | Out-Null
    if (Test-Path $File) {
        Copy-Item $File (Join-Path $Out "$($Shot[0]).png") -Force
        Write-Output ("{0,-9} {1:N0} KB" -f $Shot[0], ((Get-Item (Join-Path $Out "$($Shot[0]).png")).Length / 1KB))
    } else {
        Write-Output ("{0,-9} NO SCREENSHOT" -f $Shot[0])
    }
}
