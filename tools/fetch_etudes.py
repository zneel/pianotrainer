"""Downloads (or verifies) Chopin's Etudes Op. 10 and Op. 25 for the built-in etude library: 24 Humdrum **kern files
(~0.6 MB) of the first editions (Op. 10: Schlesinger, Op. 25: Lemoine), encoded by the Fryderyk Chopin Institute,
from https://github.com/pl-wnifc/humdrum-chopin-first-editions (CC BY 4.0) into Scores/Etudes/Chopin, plus its
licence. Verovio reads **kern directly. Existing files with the right size are kept.

usage: py -I tools/fetch_etudes.py [--verify]
"""
import json
import pathlib
import re
import sys
import urllib.parse
import urllib.request

REPO = "pl-wnifc/humdrum-chopin-first-editions"
DEST = pathlib.Path(__file__).resolve().parent.parent / "Scores" / "Etudes" / "Chopin"
KEEP = re.compile(r"^kern/(010-1b-Sm|025-1b-LE)-0(0[1-9]|1[0-2])\.krn$|^LICENSE\.txt$")


def tree():
    url = f"https://api.github.com/repos/{REPO}/git/trees/main?recursive=1"
    with urllib.request.urlopen(url) as r:
        entries = json.load(r)["tree"]
    return [(e["path"], e["size"]) for e in entries if e["type"] == "blob" and KEEP.match(e["path"])]


def local(path):
    return DEST / pathlib.PurePosixPath(path).name


def main():
    files = tree()
    missing = [p for p, size in files if not local(p).is_file() or local(p).stat().st_size != size]
    print(f"{len(files)} files, {sum(s for _, s in files) // 1024} KB; {len(files) - len(missing)} present and intact, {len(missing)} missing or wrong size")
    if "--verify" in sys.argv or not missing:
        return 1 if missing else 0
    DEST.mkdir(parents=True, exist_ok=True)
    for path in missing:
        url = f"https://raw.githubusercontent.com/{REPO}/main/" + urllib.parse.quote(path)
        with urllib.request.urlopen(url) as r:
            local(path).write_bytes(r.read())
    print(f"  fetched {len(missing)} into {DEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
