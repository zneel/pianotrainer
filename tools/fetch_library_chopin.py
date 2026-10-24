"""Downloads (or verifies) Chopin's solo piano works for the Library: the first editions encoded by the Fryderyk
Chopin Institute, https://github.com/pl-wnifc/humdrum-chopin-first-editions (CC BY 4.0), Humdrum **kern, one
edition per piece (~170 pieces, ~4 MB), into Scores/Library/Chopin with its licence and a catalog the app lists
(Scores/Library/Chopin/catalog.json: composer, collection, title, opus, credit, from each file's RISM header).

The Etudes Op. 10 and Op. 25 are left out (tools/fetch_etudes.py has them), and so are the works that are not for
piano alone: with orchestra (Op. 2, 11, 13, 14, 21, 22), chamber music (Op. 3, 8, 65, the Grand Duo), two pianos
(Op. 73) and songs (Op. 74). Most pieces exist in two or three first editions (Paris, Leipzig, London); the edition
that has the most of its opus is taken, so a set stays in one edition.

Every piece is then loaded through Verovio (tools/check_library.py --prune): the ones that do not load are left out
of the catalog and listed in skipped.json (the two posthumous polonaises in G minor and G-sharp minor stop Verovio
6.3's Humdrum import on an assertion).

usage: py -I tools/fetch_library_chopin.py [--verify]
"""
import collections
import json
import pathlib
import re
import subprocess
import sys
import urllib.parse
import urllib.request

REPO = "pl-wnifc/humdrum-chopin-first-editions"
DEST = pathlib.Path(__file__).resolve().parent.parent / "Scores" / "Library" / "Chopin"
NOT_SOLO = {"002", "003", "008", "011", "013", "014", "021", "022", "065", "073", "074", "GDC"}
SKIP_WORKS = {"072sep"}  # Op. 72 printed piece by piece: the same works as the 072 set, numbered differently
ETUDES = {"010", "025"}
# rism-genre -> the Library's collection, in the Library's order
COLLECTIONS = {
    "Ballade": "Ballades", "Scherzo": "Scherzi", "Nocturne": "Nocturnes", "Prelude": "Preludes",
    "Impromptu": "Impromptus", "Mazurka": "Mazurkas", "Polonaise": "Polonaises", "Waltz": "Waltzes",
    "Sonata": "Sonatas", "Rondo": "Rondos", "Variations": "Variations", "Variation": "Variations",
}
ORDER = list(dict.fromkeys(COLLECTIONS.values())) + ["Other works"]
GENRE_WORDS = {"preludium": "Prelude", "prelude": "Prelude", "mazurka": "Mazurka", "valse": "Waltz", "waltz": "Waltz",
               "polonaise": "Polonaise", "nocturne": "Nocturne", "rondo": "Rondo", "sonate": "Sonata",
               "sonata": "Sonata", "variations": "Variations", "ecossaise": "Ecossaise", "écossaise": "Ecossaise",
               "écossaises": "Ecossaise", "éccosaise": "Ecossaise", "marche": "Funeral March", "fantaisie": "Fantaisie",
               "allegro": "Allegro de concert"}
GENRE_NAMES = {"Variation": "Variations"}
# keys the files do not state (no rism-key and no key designation in the music)
KEY_FIXES = {("028", "1"): "C major", ("028", "9"): "E major", ("005", ""): "F major", ("046", ""): "A major",
             ("049", ""): "F minor", ("PolGm", ""): "G minor", ("PolGsharpm", ""): "G-sharp minor", ("VGNA", ""): "E major",
             ("067", "1"): "G major", ("067", "2"): "G minor", ("067", "3"): "C major", ("067", "4"): "A minor",
             ("072", "2"): "C minor", ("072", "4"): "G major", ("072", "5"): "D-flat major", ("WaltzEm", ""): "E minor"}
ROMAN = ["I", "II", "III", "IV", "V", "VI"]


def tree():
    url = f"https://api.github.com/repos/{REPO}/git/trees/main?recursive=1"
    with urllib.request.urlopen(url) as r:
        return [(e["path"], e["size"]) for e in json.load(r)["tree"] if e["type"] == "blob"]


def parse(path):
    """kern/<work>[-<volume>]-<edition>-<publisher>[-<number>][-<label>].krn"""
    if not (path.startswith("kern/") and path.endswith(".krn")):
        return None
    parts = pathlib.PurePosixPath(path).stem.split("-")
    work, i = parts[0], 1
    if "_" in work and i < len(parts) and parts[i].isdigit() and len(parts[i]) <= 2:
        work, i = work + "-" + parts[i], i + 1  # "028_1-12": the first volume of Op. 28
    if i + 1 >= len(parts):
        return None
    edition, publisher, rest = parts[i], parts[i + 1], parts[i + 2:]
    number = next((p for p in rest if re.fullmatch(r"\d{3}", p)), "")
    label = "-".join(p for p in rest if not re.fullmatch(r"\d{3}", p))
    opus = work.split("_")[0]
    return dict(work=work, opus=opus, edition=edition, publisher=publisher,
                key=str(int(number)) if number else label, path=path)


def choose(files):
    """One edition per piece: (opus, piece) -> (path, size)."""
    pieces = collections.defaultdict(list)
    for path, size in files:
        p = parse(path)
        if not p or p["opus"] in NOT_SOLO | ETUDES or p["work"] in SKIP_WORKS:
            continue
        pieces[(p["opus"], p["key"])].append((p, size))
    # an opus of one piece may be numbered "1" in one edition and not at all in another
    for opus in {o for o, _ in pieces}:
        keys = [k for o, k in pieces if o == opus]
        if set(keys) == {"", "1"}:
            pieces[(opus, "")] += pieces.pop((opus, "1"))
    coverage = collections.Counter()
    for (opus, _), editions in pieces.items():
        for p, _ in editions:
            coverage[(opus, p["work"], p["edition"], p["publisher"])] += 1
    chosen = {}
    for (opus, key), editions in pieces.items():
        p, size = max(editions, key=lambda e: (coverage[(opus, e[0]["work"], e[0]["edition"], e[0]["publisher"])],
                                               e[0]["work"] == opus, -len(e[0]["edition"])))
        chosen[(opus, key)] = (p["path"], size)
    return chosen


def read_text(path):
    data = path.read_bytes()
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError:
        return data.decode("cp1252", errors="replace")


def header(text):
    fields = {}
    for line in text.splitlines():
        m = re.match(r"^!!!([^:]+):\s*(.*)$", line)
        if m and m[1] not in fields:
            fields[m[1]] = m[2].strip()
    return fields


def key_from_music(text):
    """The first key designation, e.g. *E-: -> E-flat major, *c#: -> C-sharp minor."""
    m = re.search(r"(?m)(?:^|\t)\*([A-Ga-g])([#-]?):(?=\t|$)", text)  # in any spine
    if not m:
        return ""
    name = m[1].upper() + {"#": "-sharp", "-": "-flat", "": ""}[m[2]]
    return f"{name} {'major' if m[1].isupper() else 'minor'}"


def genre_from_title(title):
    words = re.findall(r"[a-zéè]+", title.lower())
    return next((GENRE_WORDS[w] for w in words if w in GENRE_WORDS), "")


def main():
    files = tree()
    chosen = choose(files)
    total = sum(size for _, size in chosen.values())
    local = lambda p: DEST / pathlib.PurePosixPath(p).name
    missing = [p for p, size in chosen.values() if not local(p).is_file() or local(p).stat().st_size != size]
    print(f"{len(chosen)} pieces, {total // 1024} KB; {len(chosen) - len(missing)} present and intact, {len(missing)} to fetch")
    if "--verify" in sys.argv:
        return 1 if missing else 0
    DEST.mkdir(parents=True, exist_ok=True)
    raw = f"https://raw.githubusercontent.com/{REPO}/main/"
    for path in missing:
        with urllib.request.urlopen(raw + urllib.parse.quote(path)) as r:
            local(path).write_bytes(r.read())
    with urllib.request.urlopen(raw + "LICENSE.txt") as r:
        (DEST / "LICENSE.txt").write_bytes(r.read())
    keep = {local(p).name for p, _ in chosen.values()} | {"LICENSE.txt", "catalog.json"}
    for stale in DEST.iterdir():
        if stale.name not in keep:
            stale.unlink()  # a different edition chosen before

    # what each file says about itself; an opus's genre helps the pieces that do not say
    info = {}
    for (opus, key), (path, _) in chosen.items():
        text = read_text(local(path))
        h = header(text)
        # the file's own name and title before the opus's genre (Op. 72 is a nocturne, a march and three ecossaises)
        genre = (h.get("rism-genre", "").split(",")[0].strip() or genre_from_title(pathlib.PurePosixPath(path).stem.replace("-", " "))
                 or genre_from_title(h.get("OTL", "")))
        info[(opus, key)] = dict(h=h, genre=GENRE_NAMES.get(genre, genre), path=path,
                                 key=h.get("rism-key", "") or key_from_music(text) or KEY_FIXES.get((opus, key), ""))
    opus_genre = {}
    for (opus, _), i in info.items():
        if i["genre"]:
            opus_genre.setdefault(opus, collections.Counter())[i["genre"]] += 1
    pieces = []
    for (opus, key), i in info.items():
        genre = i["genre"] or (opus_genre[opus].most_common(1)[0][0] if opus in opus_genre else "")
        h = i["h"]
        n_in_opus = sum(1 for o, _ in info if o == opus)
        opus_label = f"Op. {int(opus)}" if opus.isdigit() else "Posthumous"
        if genre == "Sonata" and key.isdigit():
            subtitle = f"{opus_label} · {ROMAN[int(key) - 1]}"  # a movement
        elif n_in_opus > 1 and key.isdigit():
            subtitle = f"{opus_label} No. {int(key)}"
        else:
            subtitle = opus_label
        title = f"{genre} in {i['key']}" if genre and i["key"] else (genre or h.get("OTL", "").strip(" .").title() or "Piece")
        publisher = ", ".join(x for x in (h.get("PPR", ""), h.get("PPP", "")) if x)
        pieces.append({
            "file": "Library/Chopin/" + local(i["path"]).name,
            "composer": "Chopin",
            "collection": COLLECTIONS.get(genre, "Other works"),
            "title": title,
            "subtitle": subtitle,
            "credit": f"First edition{(': ' + publisher) if publisher else ''}. Encoded by the Fryderyk Chopin Institute, CC BY 4.0.",
            "_order": (ORDER.index(COLLECTIONS.get(genre, "Other works")), int(opus) if opus.isdigit() else 999,
                       int(key) if key.isdigit() else 0, key),
        })
    pieces.sort(key=lambda p: p.pop("_order"))
    (DEST / "catalog.json").write_text(json.dumps({"pieces": pieces}, ensure_ascii=False, indent=1), encoding="utf-8")
    counts = collections.Counter(p["collection"] for p in pieces)
    print("catalog:", ", ".join(f"{c} {counts[c]}" for c in ORDER if counts[c]))
    # every piece through Verovio as the app loads it; the ones that fail are left out of the catalog
    return subprocess.run([sys.executable, "-I", str(pathlib.Path(__file__).with_name("check_library.py")), "--prune", "Chopin"]).returncode


if __name__ == "__main__":
    sys.exit(main())
