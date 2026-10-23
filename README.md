# Piano Trainer

A piano learning app for **Windows and macOS**, built natively in **Unreal Engine 5.8**: load a score (MusicXML from
MuseScore / musescore.com, MEI, Humdrum, ABC), read it as large, scrolling sheet music above a keyboard, and play
along on a MIDI keyboard (or the computer keyboard). It **waits for you** until you play the right notes, plays the
other hand for you, loops the hard bars, and scores your timing.

On top of the practice screen it has:
- a **practice plan after Bernhard's method** (Piano Street forum): the piece cut into small sections, the
  seven-times test, "practice starts when you get it right", stop while it is perfect, 20 minutes at most per
  section, joins between sections, learned → mastered → never forgotten, and a **practice diary**;
- **Chopin's 24 Études** (Op. 10 and Op. 25, from the first editions) with **drills after Cortot's working method**:
  outline, dotted rhythms, impulse groups, block-chord positions and moving accents, generated from the score.

| Piece | What it does |
|---|---|
| **Verovio 6.3.0** (LGPL, separate DLL/dylib) | Engraves the score as one long system and gives every note's pitch, onset and duration. Patched: Humdrum key-signature accidentals (see below). |
| **lunasvg 3.5.0** (MIT, static) | Rasterises Verovio's SVG into texture tiles on a background task; finds each notehead and staff. |
| **sfizz 1.2.3** (BSD-2, separate DLL/dylib) | The piano: plays the Salamander Grand Piano's own SFZ file (velocity layers, release noises, string resonance, pedal noises, the damper) with up to 1024 voices, streaming the samples from disk. |
| **Salamander Grand Piano V3** (CC-BY 3.0, Alexander Holm) | Yamaha C5, 16 velocity layers × 30 keys, FLAC, ~713 MB (downloaded by `tools/fetch_salamander.py`). |
| **Chopin first editions** (CC BY 4.0, Fryderyk Chopin Institute) | Études Op. 10 and Op. 25 in Humdrum `**kern` (`tools/fetch_etudes.py`). |
| **PianoKit Sequencer** (our MetaSound node) | The practice engine on the audio thread: accompaniment, your keys, wait mode (the clock stops exactly at the next chord), play-along scoring (±220 ms, latency-compensated), count-in clicks, tempo, seek, loop. |
| **PianoKit Sfizz** (our MetaSound node) | Feeds the sequencer's MIDI stream into sfizz sample-accurately and renders it; also the count-in clicks. |
| **Harmonix** / **MIDI Device Support** (Epic plugins) | MIDI stream types between the nodes; every connected MIDI keyboard is bound automatically (CC64 = sustain pedal). |
| Slate UI (`Source/PianoTrainer/UI`) | Menu, practice screen, sheet strip, keyboard, optional falling notes, sidebar (Practice / Plan / Visual), transport, practice coach. No editor assets are needed: the MetaSound graph is built in code. |

## Using it

**Menu.** *Home*: continue, the Chopin Études, open a score, and the pieces practised recently. *Études*: each
étude's key, nickname, what it trains and its suggested drills. *Diary*: practice days in the last week, what is due
today (sections to check or play through, pieces to relearn), each plan's progress and the latest sessions; click an
item to practise it. *Library*: the bundled samples and recent files. `Esc` toggles the menu.

**Practice screen.** Sheet music on top (two thirds of the window; all of it when the falling notes are off), the
keyboard under it, the measure ruler (click a measure to jump, drag across measures to loop them; the plan's sections
are drawn along its top: grey new, gold learned, green mastered) and the transport bar (piece and progress, measure
counter, restart / measure back / play-pause / measure forward, speed knob, loop) at the bottom, and the sidebar:
- *Practice tab:* mode (**Wait** for me / **Play along** / **Listen**), hand, my keys sound, input latency, count-in
  (1 or 2 bars of clicks), **drills** (★ = suggested for this étude; the accent drills also report how much louder
  you played the accents, from MIDI velocity), loop section, and the two hands' note counts.
- *Plan tab:* the practice plan (below).
- *Visual tab:* sheet music, falling notes (off by default), sparks, colour by hand, how long notes fall.

Colours: the right hand is gold, the left copper; notes you play turn green, missed ones red; the hand the app plays
for you is dimmer. On the keyboard the next keys glow in their hand's colour, held keys turn green when right and red
when wrong.

Keys: `Space` play/pause, `Backspace` restart, `↑`/`↓` tempo, `←`/`→` seek 2 s, `1`/`2`/`3` right/left/both,
`Q` wait mode, `V` listen, `M` my keys sound, `Ctrl+O` open. No MIDI keyboard? `A W S E D F T G Y H U J K O L P ;`
= C4 to E5, `Z`/`X` octave, `Left Shift` = sustain pedal; or click the keys.

Open a score with `Ctrl+O`, the menu, or by dropping a file on the window: `.musicxml` `.xml` `.mxl` `.mei` `.krn`
`.abc`; `.mscz`/`.mscx` are converted with MuseScore Studio's command line if it is installed.

### The practice plan (after Bernhard)

Bernhard, a teacher on the Piano Street forum (2003-08), described a way of learning a piece in many posts; the
app's Plan tab applies it (`docs/bernhard-method.md` collects the sources):

- **Sections.** *Make a plan* cuts the piece into two-bar sections, one bar where the music is clearly denser than
  the rest. Each section is practised with the first beat of the next bar, so that the sections join up.
- **Sessions.** New sections and joins alternate: 1, 2, 1–2, 3, 1–3, 4, 1–4, 5, 2–5, ... (a join spans at most four
  sections). The NEXT card starts the next one; any section can be started from the list.
- **The seven-times test.** A session loops its section at tempo with a one-bar count-in. Not right once in seven
  times? The section is too big: *Cut in half* splits it (down to a single beat) and starts again on the first half.
- **Practice starts when you get it right.** After the first clean pass, play it right at least seven times and at
  least as often as it went wrong before. A mistake after it was right is fatigue: the coach says so.
- **Stop while it is perfect.** The session ends on a clean repetition: the section is *learned*.
- **20 minutes at most** per section (a timer): not there by then, cut it in half.
- **Mastered.** On a later day, the first time through a learned section decides: perfect straight away = mastered;
  otherwise practise it again (it goes faster than the first time). Mastered sections are due for a play-through
  when they have not been played for three days.
- **Never forgotten.** A month or more after a mastered section was last played it is relearned from scratch; after
  the third relearning it is *omniscient*.
- **Note-groups.** For a stubborn spot: units of four notes of the practised hand, overlapping by one, played as
  1, 2, 3, ... then 1–2, 2–3, ... then longer, each twice right, up to the whole section.
- **Tempo.** Aim at the final tempo or faster (70 / 85 / 100 / 110 % chips); the best clean tempo is kept per
  section. Slow work belongs to the drills (outline, rhythms), not to the session.
- **Diary.** Every session is written to `Saved/PianoDiary.json` (date, section, minutes, passes, clean ones, tempo,
  result); the Diary page counts practice days (Bernhard: at least three a week).

### Drills (after Cortot)

Alfred Cortot's working editions of the Études (1915-17) prepare each difficulty with rhythmic variants, groupings
and chord positions before the étude is played as written. His exercises themselves are still under copyright in
Europe (he died in 1962), so the app **generates** the same kinds of drills from the score instead. Each drill
re-times or re-accents the practised hand's notes inside each beat (the accompaniment stays as written, so the sheet
and the measure counter stay in step):

| Drill | What changes |
|---|---|
| Outline | only each beat's first note, held through the beat (the skeleton of a run or figure) |
| Dotted: long–short / short–long | each pair of notes in a beat becomes 3:1 / 1:3 |
| Impulses | each beat's notes as one quick gesture in its first third, then held |
| Block chords | each beat's notes struck together (hand positions of arpeggios and broken chords) |
| Accent every 3rd / 4th | accents that walk across the beat (velocity +30) |

## Getting the sources and data

Nothing large is in git; scripts fetch it, pinned by hash or size:

```
py -I tools/fetch_thirdparty.py     # Verovio 6.3.0, lunasvg 3.5.0, sfizz 1.2.3 sources (~26 MB) -> ThirdParty/src
py -I tools/fetch_salamander.py     # Salamander Grand Piano V3, 665 files, ~713 MB -> Plugins/PianoKit/ThirdParty/Salamander
py -I tools/fetch_etudes.py         # Chopin Études, 24 Humdrum files, ~0.6 MB -> Scores/Etudes/Chopin
```

## Building

Engine: UE 5.8. Windows needs Visual Studio 2026 with the C++ workload; macOS needs Xcode. No CMake.

1. Third-party libraries: `ThirdParty\build_win64.bat` (plain `cl`; `build_win64.bat sfizz` rebuilds only sfizz)
   or `ThirdParty/build_mac.sh` (universal arm64 + x86_64 `clang`). Both apply `ThirdParty/patches` first and put
   the results in `Plugins/PianoKit/ThirdParty`.
2. Editor build (Windows):
   `<UE>\Engine\Build\BatchFiles\Build.bat PianoTrainerEditor Win64 Development -Project="<path>\PianoTrainer.uproject"`
3. Run: `UnrealEditor.exe PianoTrainer.uproject -game -windowed -resx=1600 -resy=900`
4. Package (shipping):
   `RunUAT.bat BuildCookRun -project="<path>\PianoTrainer.uproject" -platform=Win64 -clientconfig=Shipping -build -cook -stage -pak -archive -archivedirectory="<path>\Packaged" -noP4 -unattended`
   The Salamander folder, sfizz, Verovio and its data, and `Scores/` are staged as loose files next to the app.

macOS: `./ThirdParty/build_mac.sh`, then `Build.sh PianoTrainerEditor Mac Development -Project=...` and
`RunUAT.sh BuildCookRun ... -platform=Mac` from your UE 5.8 install. sfizz and Verovio are loaded with `dlopen` and
their functions looked up by name (`SfizzApi.cpp`, `VerovioApi.cpp`), so no rpath setup is needed. (Not yet tried on
a Mac.)

The optional engine-rendered stage (`-keyboard3d`: a 3D keyboard and a falling-notes highway with bloom) needs two
materials made by a commandlet: `UnrealEditor-Cmd.exe PianoTrainer.uproject -run=PianoAssetBuilder -unattended -nullrhi`.

### Patches to third-party code

- **Verovio, Humdrum import** (`ThirdParty/patches/verovio-6.3.0-humdrum-gestural-accidentals.patch`): accidentals
  implied by the key signature were neither drawn nor stored as performed accidentals, so every `**kern` file sounded
  (and was judged) without its key signature — Op. 10 No. 5 "Black Keys" came out on white keys. The patch keeps them
  as `accid.ges`. MusicXML is not affected.
- **sfizz voice cap** 256 → 1024 (`config::maxVoices`; every use is a capacity), applied by the build scripts.

## Testing with data

`tools/run_tests.ps1` runs the battery below (each test plays itself and writes `Saved/PianoLogs/tests/<name>.json`,
plus a `.wav` where it records); `-Exe` points it at a packaged build, `-Only a,b` picks tests (run it with
`powershell -Command "& tools/run_tests.ps1 -Only a,b"`), `-Stress <score>` adds a 200 % listen run of a long, dense
piece.

- `-autotest` plays the practised hands perfectly, then writes `Saved/PianoLogs/autotest.json` (expected / hits /
  wrong / missed / waits / timing error / frame times / scroll smoothness) and quits. With `-wait=0|1`,
  `-hands=right|left|both`, `-tempo=0.8`, `-score="path"`, `-start=` / `-stop=<ms>`, `-autoerrors=N` (every Nth
  note a semitone off), `-drill=outline|longshort|shortlong|impulses|blockchords|accent3|accent4`, `-countin=<bars>`,
  `-recordwav` (master output to `autotest.wav`), `-shot=<ms>` (screenshot), `-pianolog` (event CSV).
- `-coach=<n>` / `-coach=s<k>` (with `-autotest`) runs session *n* of the piece's plan, or section *k* alone, until
  the coach says done or too big, and adds the coach's phases, passes, the section's stage and the diary to the
  report; `-coachgroups` starts note-groups, `-coachsplit` cuts a too-big section in half, `-diary=<file>` keeps the
  test's diary apart from yours. The battery runs learn → next-day check (the learned date moved back a day) → join →
  note-groups → too big + split → relearn after 40 days.
- `-uishot=<s>` saves a screenshot of whatever is on screen (the menu too) and quits; `-menupage=etudes|diary|library`
  and `-sidebartab=practice|plan|visual` open the app on that page or tab.
- `tools/wav_clicks.py`, `wav_levels.py`, `wav_onsets.py` measure recordings; `tools/vrv_probe.py` runs a score
  through Verovio outside UE.

Results on Windows 11, editor build `-game`, 1600×900 (2026-10-24; frame p99 is under 3.1 ms and no frame took
over 33 ms in any run):

| Test | Outcome | Notes |
|---|---|---|
| `ode_wait` | finished | 45/45 hits, 0 wrong, 0 missed, timing error 9.4 ms |
| `ode_drill_ls` / `ode_drill_acc` | finished | 45/45 each; the accent drill measured +48 velocity on the accents |
| `ode_outline` | finished | 42/45: one note per beat, the rest silent |
| `ode_countin` | stopped at 6 s | clicks at 0, 0.6, 1.2, 1.8 s, the first chord at 2.41 s (from the recording) |
| `etude_10_5` (right hand) | stopped at 20 s | 230 hits, 0 wrong, 0 missed (G♭ major: the key-signature patch) |
| `coach_learn` | done | m. 1–2: 7 times through, right from the first, 7 right → learned |
| `coach_cold` | done | learned "yesterday", perfect the first time → mastered after 1 pass |
| `coach_join` | done | join m. 1–3, 7 right |
| `coach_groups` | done | note-groups 1, 2 (twice right each), then the whole section, 7 right |
| `coach_tired` | done | right, then mistakes came back twice → stopped on the next clean pass (4 of 6 right) |
| `coach_toobig` | split | wrong every 3rd note: 7 times and never right → too big → cut in half (5 → 6 sections) |
| `coach_relearn` | done | mastered, last played 40 days ago, relearned for the third time → omniscient |
| `coach_hardest` | done | Op. 10 No. 3: the plan starts at m. 17 (its densest bar), 7 right |
| stress (Chopin, 200 %, listen) | finished | 3168 notes, 0 frames over 33 ms, smooth-scroll jitter p99 1.66 ms |

The Shipping package gives the same results for `ode_countin` (same click and note onsets in its recording),
`etude_10_5` and `coach_learn`. A packaged build keeps its `Saved` folder (reports, diary) in
`%LOCALAPPDATA%\PianoTrainer\Saved`; `run_tests.ps1 -Exe <packaged exe>` reads it from there.

## Layout

```
PianoTrainer.uproject
Config/                      audio buffers (256 frames x 3), cooking
docs/                        bernhard-method.md: the practice method the plan follows, with sources
Plugins/PianoKit/
  Source/PianoKit/           FPianoScore (Verovio + lunasvg), FPianoSession, sequencer + sfizz MetaSound nodes,
                             UPianoPlayer (graph, sound, MIDI, feedback, drills, loop passes, async score loading),
                             FPianoDrills, FPianoLibrary, FPianoPlan + FPianoDiary (practice plans and diary)
  Source/PianoKitEditor/     PianoAssetBuilder commandlet (materials for -keyboard3d)
  ThirdParty/                built libraries (not in git) + Salamander samples (fetched) and licence
Source/PianoTrainer/
  UI/                        SPianoApp, SPianoHome, SSheetStrip, SPianoKeyboard, SFallingNotes, SPianoSidebar,
                             SPianoPlanPanel, FPianoCoach, SPianoTransport, SPianoRuler, SPianoHud, PianoWidgets,
                             PianoStyle
  Piano3D/                   optional engine-rendered keyboard + note highway (-keyboard3d)
Scores/                      sample scores, Etudes/Chopin (fetched)
ThirdParty/                  build scripts and patches; src/ and build/ are local
tools/                       fetch scripts, test battery, score generators, wav analysis, vrv_probe
```

## Open items

- Not yet tested: a real MIDI keyboard and its pedal, and the macOS build.
- The three Nouvelles Études are not included: no openly licensed machine-readable edition was found.
- `.mscz` needs MuseScore Studio installed; plain `.mid` files are refused (no notation to engrave).
- From `docs/bernhard-method.md` §4, not built yet: hands separate → together inside a session (and alternating
  hands every ~30 s), a dropping-notes join mode, a personal "seven" (Bernhard: 5-9 for most people), a
  jump-up-and-back tempo ladder, a hidden-sheet memory pass, free-text diary notes with a "tomorrow" line, folding
  repeated bars into one section, and a daily queue across pieces.

## Licences

Verovio is LGPL-3.0 and sfizz is BSD-2 (with abseil, Apache-2.0); both are shipped as their own shared libraries
with their licences next to them. lunasvg and plutovg are MIT. Harmonix and MIDI Device Support are Epic plugins.
The piano samples are Salamander Grand Piano V3 by Alexander Holm, CC-BY 3.0 (credited in the app and in
`Plugins/PianoKit/ThirdParty/Salamander`). The Chopin first editions are © The Fryderyk Chopin Institute, CC BY 4.0
(`Scores/Etudes/Chopin/LICENSE.txt`). Cortot's editions are not used. The app's own code has no licence file yet.
