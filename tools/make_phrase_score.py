"""Writes Scores/test_phrases.musicxml: 16 bars whose phrases are known, for the practice-plan tests.

  m. 1-4      phrase A: slurred, ends on a long note and a rest (half cadence)
  m. 5-8      phrase A again, note for note (learned with the first), then a double bar
  m. 9-12     eighth-note figuration between repeat signs
  m. 13-14.3  a phrase ending on a fermata and a rest
  m. 14.4-16  a phrase starting on an upbeat (beat 4 of bar 14), final bar line

usage: py -I tools/make_phrase_score.py
"""
import pathlib

DIV = 2  # divisions per quarter
DUR = {"8": 1, "4": 2, "2": 4, "1": 8}
TYPE = {"8": "eighth", "4": "quarter", "2": "half", "1": "whole"}

A_RH = [
    [("C", 5, "4", "slur-start"), ("D", 5, "4"), ("E", 5, "4"), ("F", 5, "4")],
    [("G", 5, "4"), ("A", 5, "4"), ("G", 5, "4"), ("F", 5, "4")],
    [("E", 5, "4"), ("F", 5, "4"), ("D", 5, "4"), ("E", 5, "4")],
    [("D", 5, "2", "slur-stop"), ("rest", 0, "2")],
]
A_LH = [
    [("C", 3, "2"), ("G", 2, "2")],
    [("F", 2, "2"), ("C", 3, "2")],
    [("C", 3, "2"), ("G", 2, "2")],
    [("G", 2, "2"), ("rest", 0, "2")],
]


def figure(*steps):
    return [(s, o, "8") for s, o in steps * 2]


RH = A_RH + A_RH + [
    figure(("C", 5), ("E", 5), ("G", 5), ("E", 5)),
    figure(("D", 5), ("F", 5), ("A", 5), ("F", 5)),
    figure(("E", 5), ("G", 5), ("B", 5), ("G", 5)),
    figure(("F", 5), ("A", 5), ("C", 6), ("A", 5)),
    [("C", 5, "4", "slur-start"), ("E", 5, "4"), ("G", 5, "4"), ("C", 6, "4")],
    [("G", 5, "2", "slur-stop fermata"), ("rest", 0, "4"), ("G", 4, "4", "slur-start")],
    [("C", 5, "4"), ("D", 5, "4"), ("E", 5, "4"), ("F", 5, "4")],
    [("C", 5, "1", "slur-stop")],
]
LH = A_LH + A_LH + [
    [("C", 3, "4"), ("G", 3, "4"), ("E", 3, "4"), ("G", 3, "4")],
    [("D", 3, "4"), ("A", 3, "4"), ("F", 3, "4"), ("A", 3, "4")],
    [("E", 3, "4"), ("B", 3, "4"), ("G", 3, "4"), ("B", 3, "4")],
    [("F", 3, "4"), ("C", 4, "4"), ("A", 3, "4"), ("C", 4, "4")],
    [("C", 3, "1")],
    [("G", 2, "2", "fermata"), ("rest", 0, "2")],
    [("F", 2, "2"), ("G", 2, "2")],
    [("C", 3, "1")],
]

LEFT_BAR = {9: '<barline location="left"><bar-style>heavy-light</bar-style><repeat direction="forward"/></barline>'}
RIGHT_BAR = {
    8: '<barline location="right"><bar-style>light-light</bar-style></barline>',
    12: '<barline location="right"><bar-style>light-heavy</bar-style><repeat direction="backward"/></barline>',
    16: '<barline location="right"><bar-style>light-heavy</bar-style></barline>',
}


def note_xml(item, staff, voice):
    step, octave, dur = item[:3]
    marks = item[3].split() if len(item) > 3 else []
    if step == "rest":
        return (f"<note><rest/><duration>{DUR[dur]}</duration><voice>{voice}</voice><type>{TYPE[dur]}</type>"
                f"<staff>{staff}</staff></note>")
    notations = ""
    for mark in marks:
        if mark == "slur-start":
            notations += '<slur type="start" number="1"/>'
        elif mark == "slur-stop":
            notations += '<slur type="stop" number="1"/>'
        elif mark == "fermata":
            notations += '<fermata type="upright"/>'
    return (
        f"<note><pitch><step>{step}</step><octave>{octave}</octave></pitch>"
        f"<duration>{DUR[dur]}</duration><voice>{voice}</voice><type>{TYPE[dur]}</type>"
        f"<staff>{staff}</staff>" + (f"<notations>{notations}</notations>" if notations else "") + "</note>"
    )


def main():
    bars = []
    for i, (rh, lh) in enumerate(zip(RH, LH), start=1):
        parts = [f'<measure number="{i}">', LEFT_BAR.get(i, "")]
        if i == 1:
            parts.append(
                f"<attributes><divisions>{DIV}</divisions><key><fifths>0</fifths></key>"
                "<time><beats>4</beats><beat-type>4</beat-type></time><staves>2</staves>"
                '<clef number="1"><sign>G</sign><line>2</line></clef>'
                '<clef number="2"><sign>F</sign><line>4</line></clef></attributes>'
                '<direction placement="above"><direction-type><metronome><beat-unit>quarter</beat-unit>'
                '<per-minute>120</per-minute></metronome></direction-type><sound tempo="120"/></direction>'
            )
        parts += [note_xml(n, 1, 1) for n in rh]
        parts.append(f"<backup><duration>{4 * DIV}</duration></backup>")
        parts += [note_xml(n, 2, 5) for n in lh]
        parts.append(RIGHT_BAR.get(i, ""))
        parts.append("</measure>")
        bars.append("".join(parts))

    xml = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN" '
        '"http://www.musicxml.org/dtds/partwise.dtd">\n'
        '<score-partwise version="4.0"><work><work-title>Phrase test</work-title></work>'
        '<part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>'
        '<part id="P1">' + "\n".join(bars) + "</part></score-partwise>\n"
    )
    out = pathlib.Path(__file__).resolve().parent.parent / "Scores" / "test_phrases.musicxml"
    out.write_text(xml, encoding="utf-8")
    print(out)


if __name__ == "__main__":
    main()
