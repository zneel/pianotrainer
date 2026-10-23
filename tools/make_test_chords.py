"""Writes Scores/test_chords.musicxml: an original 8-bar piece that exercises the harder notation cases:
G major, 3/4, chords in both hands, a tie across a barline, a triplet, an accidental and two right-hand voices.

Prints the expected number of notes to strike (tied continuations are not struck again) per hand.
"""
import pathlib

DIV = 6  # per quarter: eighth = 3, triplet eighth = 2
TYPES = {24: "whole", 12: "half", 18: "half", 6: "quarter", 9: "quarter", 3: "eighth", 2: "eighth"}


def note(step, octave, dur, staff, voice, alter=0, chord=False, tie=None, triplet=None):
    t = TYPES[dur]
    dot = "<dot/>" if dur in (9, 18) else ""
    x = "<note>" + ("<chord/>" if chord else "")
    x += f"<pitch><step>{step}</step>" + (f"<alter>{alter}</alter>" if alter else "") + f"<octave>{octave}</octave></pitch>"
    x += f"<duration>{dur}</duration>"
    if tie:
        x += f'<tie type="{tie}"/>'
    x += f"<voice>{voice}</voice><type>{t}</type>{dot}"
    if alter == 1:
        x += "<accidental>sharp</accidental>"
    if triplet:
        x += "<time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification>"
    x += f"<staff>{staff}</staff>"
    notations = ""
    if tie:
        notations += f'<tied type="{tie}"/>'
    if triplet in ("start", "stop"):
        notations += f'<tuplet type="{triplet}"/>'
    if notations:
        x += f"<notations>{notations}</notations>"
    return x + "</note>"


def backup(d):
    return f"<backup><duration>{d}</duration></backup>"


BAR = 18  # 3/4
bars = []
struck = {1: 0, 2: 0}


def n(*a, **k):
    if k.get("tie") != "stop":
        struck[a[3]] += 1
    return note(*a, **k)


# 1: RH chord G4+B4+D5 quarter, then A4 B4 eighths... LH G2+D3 dotted half
bars.append([n("G", 4, 6, 1, 1), n("B", 4, 6, 1, 1, chord=True), n("D", 5, 6, 1, 1, chord=True),
             n("A", 4, 3, 1, 1), n("B", 4, 3, 1, 1), n("C", 5, 6, 1, 1),
             backup(BAR), n("G", 2, 18, 2, 5), n("D", 3, 18, 2, 5, chord=True)])
# 2: RH voice 1 triplet C5 B4 A4 then D5 half tied into bar 3; RH voice 2 G4 F#4 G4 quarters;
#    LH broken eighths G2 D3 B3 D3 G3 D3
bars.append([n("C", 5, 2, 1, 1, triplet="start"), n("B", 4, 2, 1, 1, triplet="continue"), n("A", 4, 2, 1, 1, triplet="stop"),
             n("D", 5, 12, 1, 1, tie="start"),
             backup(BAR), n("G", 4, 6, 1, 2), n("F", 4, 6, 1, 2, alter=1), n("G", 4, 6, 1, 2),
             backup(BAR), n("G", 2, 3, 2, 5), n("D", 3, 3, 2, 5), n("B", 3, 3, 2, 5), n("D", 3, 3, 2, 5), n("G", 3, 3, 2, 5), n("D", 3, 3, 2, 5)])
# 3: RH D5 tied continuation quarter, C#5 quarter (accidental), D5 quarter ; LH D3+F#3+A3 chord half + rest-free quarter D3
bars.append([n("D", 5, 6, 1, 1, tie="stop"), n("C", 5, 6, 1, 1, alter=1), n("D", 5, 6, 1, 1),
             backup(BAR), n("D", 3, 12, 2, 5), n("F", 3, 12, 2, 5, alter=1, chord=True), n("A", 3, 12, 2, 5, chord=True), n("D", 3, 6, 2, 5)])
# 4: RH E5 dotted half ; LH C3 E3 G3 quarters
bars.append([n("E", 5, 18, 1, 1),
             backup(BAR), n("C", 3, 6, 2, 5), n("E", 3, 6, 2, 5), n("G", 3, 6, 2, 5)])
# 5: RH chord B4+D5 quarter x3 ; LH G2 dotted half
bars.append([n("B", 4, 6, 1, 1), n("D", 5, 6, 1, 1, chord=True), n("B", 4, 6, 1, 1), n("D", 5, 6, 1, 1, chord=True),
             n("C", 5, 6, 1, 1), n("E", 5, 6, 1, 1, chord=True),
             backup(BAR), n("G", 2, 18, 2, 5)])
# 6: RH F#5 eighths: F#5 E5 D5 C5 B4 A4 ; LH D3+A3 chord dotted half
bars.append([n("F", 5, 3, 1, 1, alter=1), n("E", 5, 3, 1, 1), n("D", 5, 3, 1, 1), n("C", 5, 3, 1, 1), n("B", 4, 3, 1, 1), n("A", 4, 3, 1, 1),
             backup(BAR), n("D", 3, 18, 2, 5), n("A", 3, 18, 2, 5, chord=True)])
# 7: RH G4 quarter, A4 quarter, F#4 quarter ; LH E3 quarter C3 quarter D3 quarter
bars.append([n("G", 4, 6, 1, 1), n("A", 4, 6, 1, 1), n("F", 4, 6, 1, 1, alter=1),
             backup(BAR), n("E", 3, 6, 2, 5), n("C", 3, 6, 2, 5), n("D", 3, 6, 2, 5)])
# 8: RH G4+B4+D5+G5 dotted half ; LH G2+G3 dotted half
bars.append([n("G", 4, 18, 1, 1), n("B", 4, 18, 1, 1, chord=True), n("D", 5, 18, 1, 1, chord=True), n("G", 5, 18, 1, 1, chord=True),
             backup(BAR), n("G", 2, 18, 2, 5), n("G", 3, 18, 2, 5, chord=True)])

measures = []
for i, content in enumerate(bars, start=1):
    head = ""
    if i == 1:
        head = (f"<attributes><divisions>{DIV}</divisions><key><fifths>1</fifths></key>"
                "<time><beats>3</beats><beat-type>4</beat-type></time><staves>2</staves>"
                '<clef number="1"><sign>G</sign><line>2</line></clef><clef number="2"><sign>F</sign><line>4</line></clef></attributes>'
                '<direction placement="above"><direction-type><metronome><beat-unit>quarter</beat-unit>'
                '<per-minute>120</per-minute></metronome></direction-type><sound tempo="120"/></direction>')
    tail = '<barline location="right"><bar-style>light-heavy</bar-style></barline>' if i == len(bars) else ""
    measures.append(f'<measure number="{i}">{head}{"".join(content)}{tail}</measure>')

xml = ('<?xml version="1.0" encoding="UTF-8"?>\n<score-partwise version="4.0"><work><work-title>Test: chords, ties, triplets</work-title></work>'
       '<part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list><part id="P1">'
       + "\n".join(measures) + "</part></score-partwise>\n")
out = pathlib.Path(__file__).resolve().parent.parent / "Scores" / "test_chords.musicxml"
out.write_text(xml, encoding="utf-8")
print(out, "struck notes: right", struck[1], "left", struck[2], "total", struck[1] + struck[2])
