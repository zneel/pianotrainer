"""Loads every piece of the composers' library (Scores/Library/*/catalog.json) through our verovio.dll, as the app
does, and reports the ones that fail: no notes, no measures, or Verovio errors. Also how long each takes.

Each piece is loaded in its own process: a Verovio assertion aborts the process (and would crash the app).

usage: py -I tools/check_library.py [source ...]           (e.g. Chopin; default: all)
       py -I tools/check_library.py --prune <source>      (also drops the failing pieces from its catalog and lists
                                                           them in Scores/Library/<source>/skipped.json; the fetch
                                                           scripts run this, so the app never lists a piece that
                                                           would not load or would crash Verovio)
       py -I tools/check_library.py --one <score>         (one piece; prints a JSON line)
"""
import ctypes
import json
import pathlib
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parent.parent
VRV = ROOT / "Plugins" / "PianoKit" / "ThirdParty" / "Verovio"
SCORES = ROOT / "Scores"
OPTIONS = {"breaks": "none", "header": "none", "footer": "none", "adjustPageHeight": True, "adjustPageWidth": True,
           "scale": 100, "svgBoundingBoxes": False, "svgViewBox": False}


def toolkit():
    lib = ctypes.CDLL(str(VRV / "Win64" / "verovio.dll"))
    for name in ["vrvToolkit_renderToSVG", "vrvToolkit_renderToTimemap", "vrvToolkit_getLog"]:
        getattr(lib, name).restype = ctypes.c_char_p
    lib.vrvToolkit_constructorResourcePath.restype = ctypes.c_void_p
    for name in ["vrvToolkit_getLog", "vrvToolkit_setOptions", "vrvToolkit_loadData", "vrvToolkit_renderToSVG",
                 "vrvToolkit_renderToTimemap"]:
        getattr(lib, name).argtypes = None
    lib.vrvToolkit_loadZipDataBuffer.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.enableLogToBuffer(ctypes.c_bool(True))
    tk = ctypes.c_void_p(lib.vrvToolkit_constructorResourcePath(str(VRV / "data").encode()))
    lib.vrvToolkit_setOptions(tk, json.dumps(OPTIONS).encode())
    return lib, tk


def check(lib, tk, path):
    data = path.read_bytes()
    start = time.perf_counter()
    if path.suffix.lower() == ".mxl":
        ok = lib.vrvToolkit_loadZipDataBuffer(tk, data, len(data))
    else:
        ok = lib.vrvToolkit_loadData(tk, data)
    if not ok:
        return "load failed", 0, 0, time.perf_counter() - start
    svg = lib.vrvToolkit_renderToSVG(tk, 1, ctypes.c_bool(False)) or b""
    tm = json.loads((lib.vrvToolkit_renderToTimemap(tk, b'{"includeMeasures": true}') or b"[]").decode())
    notes = sum(len(e.get("on", [])) for e in tm)
    measures = sum(1 for e in tm if "measureOn" in e)
    seconds = time.perf_counter() - start
    log = (lib.vrvToolkit_getLog(tk) or b"").decode(errors="replace")
    errors = [l for l in log.splitlines() if l.startswith("[Error]")]
    problem = "no notes" if notes == 0 else "no measures" if measures == 0 else ("; ".join(errors)[:160] if errors else "")
    return problem, notes, len(svg), seconds


def check_isolated(path):
    """(problem, notes, seconds) from a child process; a crash is a problem too."""
    try:
        run = subprocess.run([sys.executable, "-I", __file__, "--one", str(path)], capture_output=True, text=True, timeout=300)
    except subprocess.TimeoutExpired:
        return "timeout (300 s)", 0, 300.0
    for line in run.stdout.splitlines():
        if line.startswith("{"):
            r = json.loads(line)
            return r["problem"], r["notes"], r["seconds"]
    tail = (run.stderr.strip().splitlines() or ["?"])[-1]
    return f"crashed (exit {run.returncode}): {tail[:140]}", 0, 0.0


def main():
    if sys.argv[1:2] == ["--one"]:
        lib, tk = toolkit()
        problem, notes, _, seconds = check(lib, tk, pathlib.Path(sys.argv[2]))
        print(json.dumps({"problem": problem, "notes": notes, "seconds": seconds}))
        return
    prune = sys.argv[1:2] == ["--prune"]
    args = sys.argv[2:] if prune else sys.argv[1:]
    sources = args or sorted(p.name for p in (SCORES / "Library").iterdir() if (p / "catalog.json").is_file())
    for source in sources:
        pieces = json.loads((SCORES / "Library" / source / "catalog.json").read_text(encoding="utf-8"))["pieces"]
        bad, slow, total = [], [], 0.0
        for piece in pieces:
            problem, notes, seconds = check_isolated(SCORES / piece["file"])
            total += seconds
            if problem:
                bad.append((piece["file"], piece["title"], piece["subtitle"], problem))
            if seconds > 5:
                slow.append((piece["file"], round(seconds, 1)))
        print(f"{source}: {len(pieces)} pieces, {len(pieces) - len(bad)} load with notes, {len(bad)} problems; {total:.0f} s in all")
        for b in bad:
            print("  PROBLEM", *b)
        if prune:
            failed = {b[0]: b[3] for b in bad}
            catalog = SCORES / "Library" / source / "catalog.json"
            data = json.loads(catalog.read_text(encoding="utf-8"))
            data["pieces"] = [p for p in data["pieces"] if p["file"] not in failed]
            catalog.write_text(json.dumps(data, ensure_ascii=False, indent=1), encoding="utf-8")
            (SCORES / "Library" / source / "skipped.json").write_text(json.dumps(failed, ensure_ascii=False, indent=1), encoding="utf-8")
            print(f"  pruned: {len(failed)} left out of the catalog")
        for s in slow:
            print("  slow", *s)


if __name__ == "__main__":
    main()
