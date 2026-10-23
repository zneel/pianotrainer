"""Loads a score through our verovio.dll (C API) and dumps what the app relies on:
SVG size and element-class structure, timemap entries, and MIDI values per note.

usage: py -I tools/vrv_probe.py <score> [out_dir]
"""
import ctypes
import json
import pathlib
import re
import sys
from collections import Counter

ROOT = pathlib.Path(__file__).resolve().parent.parent
VRV = ROOT / "Plugins" / "PianoKit" / "ThirdParty" / "Verovio"

# Same options the app uses for the flowkey-style single scrolling line.
OPTIONS = {
    "breaks": "none",
    "header": "none",
    "footer": "none",
    "adjustPageHeight": True,
    "adjustPageWidth": True,
    "scale": 100,
    "pageMarginTop": 50,
    "pageMarginBottom": 50,
    "pageMarginLeft": 50,
    "pageMarginRight": 50,
    "svgBoundingBoxes": False,
    "svgViewBox": False,
}


def main():
    score = pathlib.Path(sys.argv[1])
    out_dir = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "Saved" / "probe"
    out_dir.mkdir(parents=True, exist_ok=True)

    lib = ctypes.CDLL(str(VRV / "Win64" / "verovio.dll"))
    for name in ["vrvToolkit_getVersion", "vrvToolkit_renderToSVG", "vrvToolkit_renderToTimemap",
                 "vrvToolkit_getMIDIValuesForElement", "vrvToolkit_getLog", "vrvToolkit_getElementAttr",
                 "vrvToolkit_renderToMIDI"]:
        getattr(lib, name).restype = ctypes.c_char_p
    lib.vrvToolkit_constructorResourcePath.restype = ctypes.c_void_p
    for name in ["vrvToolkit_getVersion", "vrvToolkit_renderToTimemap", "vrvToolkit_getMIDIValuesForElement",
                 "vrvToolkit_getLog", "vrvToolkit_setOptions", "vrvToolkit_loadData", "vrvToolkit_renderToSVG",
                 "vrvToolkit_getPageCount", "vrvToolkit_getElementAttr", "vrvToolkit_renderToMIDI"]:
        getattr(lib, name).argtypes = None
    lib.enableLogToBuffer(ctypes.c_bool(True))

    tk = ctypes.c_void_p(lib.vrvToolkit_constructorResourcePath(str(VRV / "data").encode()))
    print("verovio", lib.vrvToolkit_getVersion(tk).decode())
    lib.vrvToolkit_setOptions(tk, json.dumps(OPTIONS).encode())
    data = score.read_bytes()
    if score.suffix.lower() == ".mxl":
        lib.vrvToolkit_loadZipDataBuffer.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        ok = lib.vrvToolkit_loadZipDataBuffer(tk, data, len(data))
    else:
        ok = lib.vrvToolkit_loadData(tk, data)
    print("load", bool(ok), "pages", lib.vrvToolkit_getPageCount(tk))
    svg = lib.vrvToolkit_renderToSVG(tk, 1, ctypes.c_bool(False)).decode()
    (out_dir / "strip.svg").write_text(svg, encoding="utf-8")
    m = re.search(r'<svg[^>]*width="([^"]+)"[^>]*height="([^"]+)"', svg)
    print("svg bytes", len(svg), "size", m.groups() if m else None)
    print("viewBox", re.findall(r'viewBox="([^"]+)"', svg)[:2])
    print("classes", Counter(re.findall(r'<g [^>]*class="([^"]+)"', svg)).most_common(25))

    tm = json.loads(lib.vrvToolkit_renderToTimemap(tk, b'{"includeMeasures": true}').decode())
    (out_dir / "timemap.json").write_text(json.dumps(tm, indent=1))
    print("timemap entries", len(tm), "first", tm[:3], "last", tm[-1])

    notes = [i for e in tm for i in e.get("on", [])]
    print("notes", len(notes))
    for nid in notes[:3]:
        print(nid, lib.vrvToolkit_getMIDIValuesForElement(tk, nid.encode()).decode(),
              lib.vrvToolkit_getElementAttr(tk, nid.encode()).decode())
    log = lib.vrvToolkit_getLog(tk)
    if log:
        print("log:", log.decode()[:2000])
    # Snippet of one note group so we can see the nesting of notehead / stem / staff.
    i = svg.find(f'id="{notes[0]}"')
    print(svg[max(0, i - 1200): i + 900])


if __name__ == "__main__":
    main()
