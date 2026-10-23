"""Writes Scores/ode_to_joy.musicxml: 8 bars, right-hand melody + left-hand bass, 100 bpm.

Beethoven's melody is public domain; this arrangement is ours. Used as the test score for the app.
"""
import pathlib

DIV = 2  # divisions per quarter
DUR = {"8": 1, "4": 2, "4.": 3, "2": 4, "1": 8}
TYPE = {"8": ("eighth", 0), "4": ("quarter", 0), "4.": ("quarter", 1), "2": ("half", 0), "1": ("whole", 0)}

# (step, octave, duration) per bar
RH = [
    [("E", 4, "4"), ("E", 4, "4"), ("F", 4, "4"), ("G", 4, "4")],
    [("G", 4, "4"), ("F", 4, "4"), ("E", 4, "4"), ("D", 4, "4")],
    [("C", 4, "4"), ("C", 4, "4"), ("D", 4, "4"), ("E", 4, "4")],
    [("E", 4, "4."), ("D", 4, "8"), ("D", 4, "2")],
    [("E", 4, "4"), ("E", 4, "4"), ("F", 4, "4"), ("G", 4, "4")],
    [("G", 4, "4"), ("F", 4, "4"), ("E", 4, "4"), ("D", 4, "4")],
    [("C", 4, "4"), ("C", 4, "4"), ("D", 4, "4"), ("E", 4, "4")],
    [("D", 4, "4."), ("C", 4, "8"), ("C", 4, "2")],
]
LH = [
    [("C", 3, "2"), ("G", 3, "2")],
    [("B", 2, "2"), ("G", 2, "2")],
    [("A", 2, "2"), ("E", 3, "2")],
    [("G", 2, "1")],
    [("C", 3, "2"), ("G", 3, "2")],
    [("B", 2, "2"), ("G", 2, "2")],
    [("A", 2, "2"), ("F", 2, "2")],
    [("G", 2, "2"), ("C", 3, "2")],
]


def note_xml(step, octave, dur, staff, voice):
    t, dots = TYPE[dur]
    return (
        f"<note><pitch><step>{step}</step><octave>{octave}</octave></pitch>"
        f"<duration>{DUR[dur]}</duration><voice>{voice}</voice><type>{t}</type>"
        + "<dot/>" * dots
        + f"<staff>{staff}</staff></note>"
    )


def main():
    bars = []
    for i, (rh, lh) in enumerate(zip(RH, LH), start=1):
        parts = [f'<measure number="{i}">']
        if i == 1:
            parts.append(
                f"<attributes><divisions>{DIV}</divisions><key><fifths>0</fifths></key>"
                "<time><beats>4</beats><beat-type>4</beat-type></time><staves>2</staves>"
                '<clef number="1"><sign>G</sign><line>2</line></clef>'
                '<clef number="2"><sign>F</sign><line>4</line></clef></attributes>'
                '<direction placement="above"><direction-type><metronome><beat-unit>quarter</beat-unit>'
                '<per-minute>100</per-minute></metronome></direction-type><sound tempo="100"/></direction>'
            )
        parts += [note_xml(s, o, d, 1, 1) for s, o, d in rh]
        parts.append(f"<backup><duration>{4 * DIV}</duration></backup>")
        parts += [note_xml(s, o, d, 2, 5) for s, o, d in lh]
        if i == len(RH):
            parts.append('<barline location="right"><bar-style>light-heavy</bar-style></barline>')
        parts.append("</measure>")
        bars.append("".join(parts))

    xml = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 4.0 Partwise//EN" '
        '"http://www.musicxml.org/dtds/partwise.dtd">\n'
        '<score-partwise version="4.0"><work><work-title>Ode to Joy</work-title></work>'
        '<identification><creator type="composer">L. van Beethoven</creator></identification>'
        '<part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>'
        '<part id="P1">' + "\n".join(bars) + "</part></score-partwise>\n"
    )
    out = pathlib.Path(__file__).resolve().parent.parent / "Scores" / "ode_to_joy.musicxml"
    out.parent.mkdir(exist_ok=True)
    out.write_text(xml, encoding="utf-8")
    print(out)


if __name__ == "__main__":
    main()
