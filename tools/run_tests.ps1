# Runs the data-driven test battery against the editor build and keeps each report in Saved/PianoLogs/tests.
# usage: powershell -Command "& tools/run_tests.ps1 [-Exe <UnrealEditor.exe or packaged exe>] [-Only name,name]"
param(
    [string]$Exe = "D:\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe",
    [string[]]$Only = @(),
    # a long, dense piece for the 200 % stress test (any MusicXML; skipped if not given)
    [string]$Stress = ""
)
$Root = Split-Path -Parent $PSScriptRoot
$IsEditor = $Exe -like "*UnrealEditor*"
# a packaged (shipping) build keeps Saved under %LOCALAPPDATA%\PianoTrainer
$Logs = if ($IsEditor) { Join-Path $Root "Saved\PianoLogs" } else { Join-Path $env:LOCALAPPDATA "PianoTrainer\Saved\PianoLogs" }
$Out = Join-Path $Root "Saved\PianoLogs\tests"
New-Item -ItemType Directory -Force $Out | Out-Null
$Etudes = Join-Path $Root "Scores\Etudes\Chopin"
$Ode = "-score=`"$Root\Scores\ode_to_joy.musicxml`""
$Phrases = "-score=`"$Root\Scores\test_phrases.musicxml`""
# the coach tests share a diary of their own, in this order: learn, next-day check, join, note-groups, too big, relearn;
# it starts from a fixed plan (below), so they do not depend on how plans are cut
$Diary = Join-Path $Out "diary_test.json"
$DiaryArg = "-diary=`"$Diary`""
# the hands tests start from the same fixed plan, in a diary of their own
$HandsDiary = Join-Path $Out "diary_hands.json"
$HandsArg = "-diary=`"$HandsDiary`""
# the plan tests make their plans in a fresh diary each time
$PlanDiary = Join-Path $Out "diary_plan.json"
$PlanArg = "-diary=`"$PlanDiary`""

$Tests = [ordered]@{
    "ode_wait"      = @("-autotest", $Ode)
    "ode_drill_ls"  = @("-autotest", $Ode, "-wait=0", "-drill=longshort")
    "ode_drill_acc" = @("-autotest", $Ode, "-wait=0", "-drill=accent3")
    "ode_outline"   = @("-autotest", $Ode, "-wait=0", "-drill=outline")
    "ode_countin"   = @("-autotest", $Ode, "-wait=0", "-countin=1", "-recordwav", "-stop=6000")
    "stress_200"    = @("-autotest", "-listen", "-tempo=2", "-recordwav", "-score=`"$Stress`"")
    "etude_10_5"    = @("-autotest", "-wait=0", "-hands=right", "-stop=20000", "-score=`"$Etudes\010-1b-Sm-005.krn`"")
    # practice plans cut at phrases; plan_phrases also edits the plan on the ruler with mouse events
    "plan_phrases"  = @("-plantest=edit", $Phrases, $PlanArg)
    "plan_ode"      = @("-plantest=report", $Ode, $PlanArg)
    "plan_10_2"     = @("-plantest=report", "-score=`"$Etudes\010-1b-Sm-002.krn`"", $PlanArg)
    "plan_10_3"     = @("-plantest=report", "-score=`"$Etudes\010-1b-Sm-003.krn`"", $PlanArg)
    "coach_learn"   = @("-autotest", $Ode, $DiaryArg, "-coach=s0")
    "coach_cold"    = @("-autotest", $Ode, $DiaryArg, "-coach=s0")
    "coach_join"    = @("-autotest", $Ode, $DiaryArg, "-coach=2")
    "coach_groups"  = @("-autotest", $Ode, $DiaryArg, "-coach=s1", "-coachgroups")
    # right at once, then mistakes come back twice: the next clean pass ends the session
    "coach_tired"   = @("-autotest", $Ode, $DiaryArg, "-coach=s1", "-autoerrors=20")
    "coach_toobig"  = @("-autotest", $Ode, $DiaryArg, "-coach=s2", "-autoerrors=3", "-coachsplit", "-coachhands=together")
    "coach_relearn" = @("-autotest", $Ode, $DiaryArg, "-coach=s0")
    # Op. 10 No. 3: a phrase plan made on the spot, its first session
    "coach_hardest" = @("-autotest", $DiaryArg, "-coach=0", "-hands=right", "-score=`"$Etudes\010-1b-Sm-003.krn`"")
    # hands together first (two mistakes in it) -> each hand alone in turns -> together again, in one session
    "coach_hands_auto"   = @("-autotest", $Ode, $HandsArg, "-coach=s3", "-autoerrors=4", "-autoerrorsuntil=8")
    # hands separately from the start: R3 L3 R2 L2 R1 L1, then together
    "coach_hands_sep"    = @("-autotest", $Ode, $HandsArg, "-coach=s2", "-coachhands=separate")
    # every note wrong, at 40 %: each turn ends on the half-minute limit, then a hand alone seven times and never right -> too big
    "coach_hands_toobig" = @("-autotest", $Ode, $HandsArg, "-coach=s4", "-coachhands=separate", "-autoerrors=1", "-tempo=0.4")
}

# what the plan tests must produce (en dashes in labels read as "-")
$PlanExpect = @{
    "plan_phrases" = [ordered]@{
        "made"   = "m. 1-4 | m. 5-8 (=1) | m. 9-12 | m. 13-16 (=3) | m. 17-18.3 | m. 18.4-20"
        "moved"  = "m. 1-3 | m. 4-8 | m. 9-12 | m. 13-16 (=3) | m. 17-18.3 | m. 18.4-20"
        "split"  = "m. 1-3 | m. 4-8 | m. 9-10 | m. 11-12 | m. 13-16 | m. 17-18.3 | m. 18.4-20"
        "merged" = "m. 1-3 | m. 4-8 | m. 9-12 | m. 13-16 (=3) | m. 17-18.3 | m. 18.4-20"
        "halved" = "m. 1-3 | m. 4-8 | m. 9-12 | m. 13-16 (=3) | m. 17-18.3 | m. 18.4-19 | m. 20"
    }
    "plan_ode" = [ordered]@{ "made" = "m. 1-4 | m. 5-8" }
}

# Ode to Joy cut into fixed sections for the coach tests: m. 1-2, 3, 4-5, 6-7, 8
function Write-FixturePlan([string]$Path) {
    $Dash = [char]0x2013
    $Sections = @(@(0, 8, "m. 1${Dash}2"), @(8, 12, "m. 3"), @(12, 20, "m. 4${Dash}5"), @(20, 28, "m. 6${Dash}7"), @(28, 32, "m. 8")) | ForEach-Object {
        [ordered]@{ first = $_[0]; end = $_[1]; stage = "new"; learned = ""; mastered = ""; last = ""; sessions = 0; relearns = 0; best = 0; difficulty = 0; label = $_[2]; repeatOf = -1 }
    }
    $Plan = [ordered]@{ piece = "ode_to_joy.musicxml"; title = "Ode to Joy"; created = (Get-Date).ToString("yyyy-MM-dd"); next = 0; sections = @($Sections) }
    $Json = [ordered]@{ plans = @($Plan); entries = @() } | ConvertTo-Json -Depth 8
    [IO.File]::WriteAllText($Path, $Json, (New-Object Text.UTF8Encoding $false))
}
function Edit-Diary([scriptblock]$Change) {
    $J = Get-Content $Diary -Raw -Encoding UTF8 | ConvertFrom-Json
    & $Change $J.plans[0].sections[0]
    [IO.File]::WriteAllText($Diary, ($J | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding $false))
}
$Prep = @{
    "coach_learn"      = { Write-FixturePlan $Diary }
    "coach_hands_auto" = { Write-FixturePlan $HandsDiary }
    # learned (and last played) yesterday: today's first time through decides whether it is mastered
    "coach_cold"    = { Edit-Diary { param($S) $S.learned = $S.last = (Get-Date).AddDays(-1).ToString("yyyy-MM-dd") } }
    # mastered, last played 40 days ago, relearned twice: the third relearning makes it omniscient
    "coach_relearn" = { Edit-Diary { param($S) $S.stage = "mastered"; $S.last = (Get-Date).AddDays(-40).ToString("yyyy-MM-dd"); $S.relearns = 2 } }
}

# one plan step as text: "m. 1-4 | m. 5-8 (=1)" (=n: comes back from section n, counting from 1)
function Format-Plan($Sections) {
    ($Sections | ForEach-Object { ($_.label -replace [char]0x2013, "-") + $(if ($_.repeatOf -ge 0) { " (=" + ($_.repeatOf + 1) + ")" } else { "" }) }) -join " | "
}

foreach ($Name in $Tests.Keys) {
    if ($Only.Count -gt 0 -and $Only -notcontains $Name) { continue }
    if ($Name -eq "stress_200" -and -not $Stress) { continue }
    if ($Prep.ContainsKey($Name)) { & $Prep[$Name] }
    if ($Name -like "plan_*") { Remove-Item $PlanDiary -ErrorAction SilentlyContinue }
    Remove-Item (Join-Path $Logs "autotest.json"), (Join-Path $Logs "autotest.wav"), (Join-Path $Logs "plantest.json") -ErrorAction SilentlyContinue
    $Args = @()
    if ($IsEditor) { $Args += "`"$Root\PianoTrainer.uproject`""; $Args += "-game" }
    $Args += @("-windowed", "-resx=1600", "-resy=900", "-unattended", "-nosplash", "-log=test_$Name.log") + $Tests[$Name]
    $Start = Get-Date
    $P = Start-Process -FilePath $Exe -ArgumentList $Args -PassThru
    $P.WaitForExit(900000) | Out-Null

    $PlanReport = Join-Path $Logs "plantest.json"
    if ($Name -like "plan_*") {
        if (-not (Test-Path $PlanReport)) { Write-Output ("{0,-14} NO REPORT" -f $Name); continue }
        Copy-Item $PlanReport (Join-Path $Out "$Name.json") -Force
        $J = Get-Content $PlanReport -Raw -Encoding UTF8 | ConvertFrom-Json
        $Units = @($J.made | ForEach-Object { ($_.end - $_.first) / 4.0 }) | Sort-Object
        Write-Output ("{0,-14} {1} sections, {2} slurs, {3} fermatas, length in 4/4 bars {4}-{5} (median {6})" -f $Name, $J.made.Count, $J.slurs, $J.fermatas, $Units[0], $Units[-1], $Units[[int]($Units.Count / 2)])
        $Expect = $PlanExpect[$Name]
        foreach ($Step in @("made", "moved", "split", "merged", "halved")) {
            if (-not $J.$Step) { continue }
            $Got = Format-Plan $J.$Step
            if ($Expect -and $Expect.Contains($Step)) {
                $Verdict = if ($Got -eq $Expect[$Step]) { "PASS" } else { "FAIL (expected $($Expect[$Step]))" }
                Write-Output ("   {0,-7} {1}  {2}" -f $Step, $Got, $Verdict)
            } else {
                Write-Output ("   {0,-7} {1}" -f $Step, $Got)
            }
        }
        continue
    }

    $Report = Join-Path $Logs "autotest.json"
    if (Test-Path $Report) {
        Copy-Item $Report (Join-Path $Out "$Name.json") -Force
        $J = Get-Content $Report -Raw | ConvertFrom-Json
        $Line = "{0,-14} {1,-12} hits {2}/{3} wrong {4} missed {5} frames>33ms {6} ({7:N0} s)" -f $Name, $J.outcome, $J.hits, $J.expected, $J.wrong, $J.missed, $J.frames_over_33ms, ((Get-Date) - $Start).TotalSeconds
        if ($J.coach_phases) {
            $Line += "  {0}: {1}, {2}/{3} clean (target {4}), groups {5}, stage {6}, sections {7}" -f $J.coach_label, $J.coach_phases, $J.coach_clean, $J.coach_passes, $J.coach_target, $J.coach_group_passes, $J.section_stage, $J.plan_sections
            if ($J.sections_after_split -ge 0) { $Line += " (split {0}->{1})" -f $J.sections_before_split, $J.sections_after_split }
            if ($J.coach_hand_turns) { $Line += ", hands {0}" -f $J.coach_hand_turns }
        }
        Write-Output $Line
    } else {
        Write-Output ("{0,-14} NO REPORT" -f $Name)
    }
    $Wav = Join-Path $Logs "autotest.wav"
    if (Test-Path $Wav) { Copy-Item $Wav (Join-Path $Out "$Name.wav") -Force }
}
