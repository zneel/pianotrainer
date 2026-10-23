"""Downloads (or verifies) the Salamander Grand Piano V3 files sfizz plays: the SFZ and its tables, the 480 note
samples (16 velocity layers x 30 keys), the 88 key-release noises, the 69 string-resonance samples and the 4
pedal noises - 665 files, ~713 MB - from
https://github.com/sfzinstruments/SalamanderGrandPiano (CC-BY 3.0, Alexander Holm) into
Plugins/PianoKit/ThirdParty/Salamander. Existing files with the right size are kept.

usage: py -I tools/fetch_salamander.py [--verify]
"""
import concurrent.futures
import json
import pathlib
import re
import sys
import urllib.parse
import urllib.request

REPO = "sfzinstruments/SalamanderGrandPiano"
DEST = pathlib.Path(__file__).resolve().parent.parent / "Plugins" / "PianoKit" / "ThirdParty" / "Salamander"
KEEP = re.compile(r"^Samples/(([A-G]#?\d+v\d+)|(rel\d+)|(harm[LSV3]+[A-G]#?\d+)|(pedal[DU]\d))\.flac$|^Data/.*\.txt$|^Salamander Grand Piano V3\.sfz$")


def tree():
    url = f"https://api.github.com/repos/{REPO}/git/trees/master?recursive=1"
    with urllib.request.urlopen(url) as r:
        entries = json.load(r)["tree"]
    return [(e["path"], e["size"]) for e in entries if e["type"] == "blob" and KEEP.match(e["path"])]


def fetch(path):
    url = f"https://raw.githubusercontent.com/{REPO}/master/" + urllib.parse.quote(path)
    target = DEST / path
    target.parent.mkdir(parents=True, exist_ok=True)
    with urllib.request.urlopen(url) as r, open(target, "wb") as f:
        f.write(r.read())
    return path


def main():
    verify_only = "--verify" in sys.argv
    files = tree()
    missing = [p for p, size in files if not (DEST / p).is_file() or (DEST / p).stat().st_size != size]
    print(f"{len(files)} files, {sum(s for _, s in files) // 1048576} MB; {len(files) - len(missing)} present and intact, {len(missing)} missing or wrong size")
    if verify_only or not missing:
        return 1 if missing else 0
    with concurrent.futures.ThreadPoolExecutor(8) as pool:
        for done, path in enumerate(pool.map(fetch, missing), 1):
            if done % 50 == 0 or done == len(missing):
                print(f"  {done}/{len(missing)} {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
