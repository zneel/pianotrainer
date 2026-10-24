"""Builds the Library's Bach, Liszt, Mendelssohn and Rachmaninoff from PDMX, the Public Domain MusicXML dataset
(Long et al., https://zenodo.org/records/15571083, dataset CC BY 4.0): MuseScore uploads marked public domain or
CC0, keeping only those whose licences do not conflict (its "no_license_conflict" subset).

No curated open edition of these composers exists in a format Verovio reads (the encoded corpora are non-commercial
or reserve their rights), so this takes the best of what MuseScore users uploaded, which varies in quality:
- solo piano only (every part a piano), no lyrics, at least 50 notes; the composer's own piano works (Bach: the
  keyboard works, BWV 772-994, the notebooks, BWV 999 and 1080; Mendelssohn: his piano opus numbers and genres);
- not an arrangement by someone else, not for two pianos or four hands, no concerto reductions, no "easy" versions,
  no jokes or annotated studies of a score;
- some sign of quality: rated 4.5 or more, or viewed 300 times or more;
- one copy per work: the same catalogue number, number and movement (or the same nickname) is one work, whatever
  the upload calls it; the best rated, then the most viewed, is kept.

It downloads PDMX.csv (225 MB) and mxl.tar.gz (1.89 GB) into a download folder (default E:/modding/downloads/pdmx,
--downloads to change; resumed and checked by MD5), takes the chosen scores out of the archive into
Scores/Library/<Composer>/ with a catalog each, then loads every one through Verovio (tools/check_library.py
--prune) and leaves out those that do not load.

usage: py -I tools/fetch_library_pdmx.py [--list] [--downloads <folder>]
       --list: only print what would be taken, by composer and collection
"""
import csv
import hashlib
import json
import math
import pathlib
import re
import subprocess
import sys
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parent.parent
LIBRARY = ROOT / "Scores" / "Library"
RECORD = "https://zenodo.org/api/records/15571083/files/{}/content"
FILES = {"PDMX.csv": "30392ccf38bb63ce70e7afae70f9c88c", "mxl.tar.gz": "49ffd75ecf5489c0be6d41182eb11ff7"}

COMPOSERS = {
    "Bach": re.compile(r"\b(j\.?\s*s\.?|johann\s+sebastian)\s*bach\b|^bach\b", re.I),
    "Liszt": re.compile(r"\bliszt\b", re.I),
    "Mendelssohn": re.compile(r"\bmendelss?ohn\b|mendelssohn-bartholdy", re.I),
    "Rachmaninoff": re.compile(r"rach?maninov|rachmaninoff|rakhmaninov", re.I),
}
NAMES = {
    "Bach": r"(by\s+)?(j\.?\s*s\.?|johann\s+sebastian)?\s*bach(\s+johann\s+sebastian|\s+j\.?\s*s\.?)?",
    "Liszt": r"(by\s+)?(franz|ferenc|f\.)?\s*liszt",
    "Mendelssohn": r"(by\s+)?(felix|f\.)?\s*mendels+h?oh?n(-bartholdy|\s+bartholdy)?(\s+f\.)?",
    "Rachmaninoff": r"(by\s+)?(sergei|serge[iïy]|s\.)?\s*rach?maninoff?v?",
}
NOT_THEIRS = re.compile(r"fanny|hensel|c\.\s*p\.\s*e|carl\s+philipp|w\.\s*f\.|wilhelm\s+friedemann|j\.\s*c\.\s*bach|"
                        r"johann\s+christian|arensky|glazunov|taneyev|tchaik|vivaldi|marcello|gounod", re.I)
EXCLUDE = re.compile(r"\barr\b|\barr\.|\barrt\b|arranged|arrangement|bearbeit|busoni|simplif|\beasy\b|beginner|"
                     r"for\s+(2|two)\s+pianos|(2|two)\s+pianos|4\s*hands|four\s+hands|\bduet\b|concert[oi]\b|conceto|"
                     r"abcm2ps|sample\s+tune|excerpt|accompaniment|\bsatb\b|\bttbb\b|choir|choral|corais|cantata|passion|"
                     r"violin|cello|viol[aã]o|guitar|flute|instruments|organ|wedding\s+march|ave\s+maria|air\s+on\s+the\s+g|"
                     # jokes, studies of the score, versions "inspired by" or "with tips": not the piece itself
                     r"impossible|except\s+it|weird|meme|consecutive|inverted|derivations|subject\s+and|repetitions|"
                     r"inspired\s+by|ellipsis|fingering|\btips\b|\bintro\b|\bvar\.\s*\d+|czerny", re.I)
# a composer's piano works: (keyboard catalogue numbers, words that name a piano work)
PIANO_WORKS = {
    "Bach": (lambda n: 772 <= n <= 994 or n in (999, 1080),
             r"invention|sinfonia|well.?tempered|\bwtc\b|wohltemperiert|french\s+suite|english\s+suite|französische|"
             r"goldberg|anna\s+magdalena|notebook|minuet|menuet|little\s+prelude|kunst\s+der\s+fuge|art\s+of\s+(the\s+)?fugue|"
             r"partita\b(?!.*violin)|anh\.?\s*\d+|italian\s+concerto|chromatic\s+fantasia"),
    "Liszt": (lambda n: True, r"."),
    "Mendelssohn": (lambda n: n in (5, 6, 7, 14, 15, 16, 19, 28, 30, 33, 35, 38, 53, 54, 62, 67, 72, 82, 83, 85, 102, 104, 105, 106, 117, 118, 119),
                    r"songs?\s+without\s+words|lied(er)?\s+ohne\s+worte|romances?\s+sans|venetian|gondel|gondola|spring\s+song|"
                    r"spinning|rondo\s+capriccioso|variations\s+s[ée]rieuses|kinderst|children|scherzo|fantas|prelude|fugue|caprice|étude|etude"),
    "Rachmaninoff": (lambda n: True, r"."),
}
# Liszt's best-known pieces are often uploaded without a catalogue number: the nickname is the work
NICKNAMES = r"mazeppa|campanella|gnomenreigen|waldesrauschen|un\s+sospiro|feux\s+follets|wilde\s+jagd|harmonies\s+du\s+soir|chasse.neige|paysage|vision|fus[ée]es|appassionata|ricordanza|liebestr\w*|consolation|mephisto|valse\s+oubli\w*|sancta\s+dorothea|b[ée]n[ée]diction|invocation|tannh\w*"
MOVEMENTS = {"prel": "prelude", "prae": "prelude", "fug": "fugue", "fuga": "fugue", "alle": "allemande", "cour": "courante",
             "sara": "sarabande", "giga": "gigue", "gigu": "gigue", "menu": "minuet", "minu": "minuet", "gavo": "gavotte",
             "bour": "bourree", "aria": "aria", "fant": "fantasia", "scher": "scherzo", "rond": "rondo", "burl": "burlesca",
             "capr": "capriccio", "sinf": "sinfonia", "toc": "toccata"}
ROMAN = {"i": 1, "ii": 2, "iii": 3, "iv": 4, "v": 5, "vi": 6, "vii": 7}
COLLECTIONS = {
    "Bach": [
        ("Well-Tempered Clavier", r"well.?tempered|wohltemperiert|\bwtc\b|clavier\s+bien|\bbwv\s*0?(84[6-9]|8[5-8]\d|89[0-3])\b"),
        ("Inventions and Sinfonias", r"invention|sinfonia|\bbwv\s*0?(7[7-9]\d|80[01])\b"),
        ("Goldberg Variations", r"goldberg|\bbwv\s*0?988\b"),
        ("French Suites", r"french\s+suite|französische|\bbwv\s*0?81[2-7]\b"),
        ("English Suites", r"english\s+suite|\bbwv\s*0?(80[6-9]|81[01])\b"),
        ("Partitas", r"partita|\bbwv\s*0?(82[5-9]|830)\b"),
        ("Notebook for Anna Magdalena", r"anna\s+magdalena|notebook|anh\.?\s*1[1-3]\d|minuet\s+in\s+g"),
        ("Little Preludes", r"little\s+prelude|\bbwv\s*0?(92[4-9]|93\d|94[0-3]|999)\b"),
        ("The Art of Fugue", r"kunst\s+der\s+fuge|art\s+of\s+(the\s+)?fugue|contrapunctus|\bbwv\s*1080\b"),
    ],
    "Liszt": [
        ("Paganini Études", r"paganini|campanella"),
        ("Transcendental Études", r"transcendental|exécution\s+transcendante|execution\s+transcendante|mazeppa|wilde\s+jagd|feux\s+follets|harmonies\s+du\s+soir|chasse.neige|paysage|vision\b|eroica\b|ricordanza|fus[ée]es|appassionata"),
        ("Concert Études", r"waldesrauschen|gnomenreigen|un\s+sospiro|concert\s+[ée]tude"),
        ("Hungarian Rhapsodies", r"hungarian\s+rhapsod|rhapsodie\s+hongroise|ungarische"),
        ("Liebesträume", r"liebestr|love\s*dream|dream\s+of\s+love|r[êe]ve\s+d.amour"),
        ("Consolations", r"consolation"),
        ("Années de pèlerinage", r"années|annees|pèlerinage|pelerinage|vallée|sonetto|petrarca|venezia|villa\s+d.este|au\s+bord|chapelle"),
        ("Transcriptions and paraphrases", r"paraphrase|transcription|reminiscence|réminiscence|fantas(y|ia|ie)\s+on|overture|symphony|schubert|wagner|verdi|mozart|donizetti|rigoletto|tannh|apparition|romeo"),
    ],
    "Mendelssohn": [
        ("Songs without Words", r"songs?\s+without\s+words|lied(er)?\s+ohne\s+worte|romances?\s+sans\s+paroles|venetian|gondel|gondola|spring\s+song|frühlingslied|spinning|op\.?\s*(19|30|38|53|62|67|85|102)\b"),
        ("Preludes and Fugues", r"prelude|fugue|op\.?\s*35\b"),
    ],
    "Rachmaninoff": [
        ("Preludes", r"prelude|op\.?\s*(23|32)\b|op\.?\s*3\s*no\.?\s*2"),
        ("Études-Tableaux", r"tableaux"),
        ("Moments musicaux", r"moments?\s+musica|op\.?\s*16\b"),
        ("Sonatas", r"sonata"),
        ("Morceaux de fantaisie", r"elegie|élégie|polichinelle|serenade|mélodie|melodie|op\.?\s*3\b"),
    ],
}
CATALOGUE = re.compile(r"\b(bwv|bmv|s\.?|op\.?)\s*(anh\.?\s*)?(\d+)", re.I)


def download(folder):
    folder.mkdir(parents=True, exist_ok=True)
    for name, md5 in FILES.items():
        path = folder / name
        if path.is_file() and file_md5(path) == md5:
            continue
        print(f"downloading {name} ...")
        have = path.stat().st_size if path.is_file() else 0
        request = urllib.request.Request(RECORD.format(name), headers={"Range": f"bytes={have}-"} if have else {})
        with urllib.request.urlopen(request) as r, open(path, "ab" if have and r.status == 206 else "wb") as out:
            while chunk := r.read(1 << 20):
                out.write(chunk)
        if file_md5(path) != md5:
            raise SystemExit(f"{name}: MD5 does not match Zenodo's; delete it and run again")


def file_md5(path):
    h = hashlib.md5()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 22):
            h.update(chunk)
    return h.hexdigest()


def catalogue(title):
    """(kind, number, "anh" or "") of the first catalogue number in a title; BMV is a common typo for BWV."""
    m = CATALOGUE.search(title)
    if not m:
        return None
    kind = {"bmv": "bwv", "s": "s", "s.": "s"}.get(m[1].lower(), m[1].lower().rstrip("."))
    return kind, int(m[3]), "anh" if m[2] else ""


def identity(composer, clean, title):
    """One key per work: catalogue number + number + movement, a nickname + number, else the title's words."""
    text = title.lower()
    cat = catalogue(text)
    number = ""
    movement = ""
    m = re.search(r"\b(prel|prae|fug|fuga|alle|cour|sara|gig|menu|minu|gavo|bour|aria|fant|scher|rond|burl|capr|sinf|toc)\w*", text)
    if m:
        movement = next((v for k, v in MOVEMENTS.items() if m[0].startswith(k)), m[0])
    roman = re.search(r"[:\-]\s*(i{1,3}|iv|v|vi{1,2})\b|\b(i{1,3}|iv|vi{0,2})\.\s", text)
    if cat:
        after = text[text.find(str(cat[1])) + len(str(cat[1])):][:14]
        n = re.search(r"^\s*(?:,|/|\.)?\s*(?:no\.?|nr\.?|n)?\s*(\d+)\b", after)
        number = n[1] if n and not re.match(r"\s*\d{3,}", after) else ""
        if roman:
            number = number or str(ROMAN[(roman[1] or roman[2])])
        return " ".join(x for x in (cat[0], cat[2], str(cat[1]), number, movement) if x)
    if composer == "Liszt":
        nick = re.search(NICKNAMES + r"|r[êe]ve\s+d.amour", text)
        if nick:
            name = "liebestr" if re.match(r"r[êe]ve", nick[0]) else re.sub(r"\W+", "", nick[0])[:8]
            # a number only where the nickname names a set (Liebestraum No. 3, Consolation No. 4, Mephisto Waltz No. 1)
            n = re.search(r"\bno\.?\s*(\d+)|\bn\.?\s*(\d+)|\b(\d)\b", text) if re.match(r"liebes|consol|mephis|valse", name) else None
            return " ".join(x for x in ("liszt", name, n and next(g for g in n.groups() if g) or "") if x)
    if composer == "Bach" and re.fullmatch(r"(a\s+)?minuet\s+in\s+g(\s+major)?", clean.lower()):
        return "bwv anh 114 minuet"  # the Minuet in G of the Notebook, uploaded under its name alone
    stop = {"in", "the", "no", "nr", "piano", "solo", "major", "minor", "dur", "moll", "for", "from", "version", "and", "of", "a"}
    return " ".join(w for w in re.findall(r"[a-z0-9]+", clean.lower()) if w not in stop)


def is_piano_work(composer, title):
    in_range, words = PIANO_WORKS[composer]
    cat = catalogue(title)
    if cat and cat[0] in ("bwv", "op") and not cat[2]:
        return in_range(cat[1])
    return bool(re.search(words, title, re.I))


def clean_title(title, composer):
    t = re.sub(r"\s+", " ", title).strip()
    t = re.sub(r"[\(\[\{][^)\]\}]*(piano\s+solo|real\s+audio|professional|production|urtext|\d{4}\s*-\s*\d{4})[^)\]\}]*[\)\]\}]", "", t, flags=re.I)
    t = re.sub(r"\bpiano\s+solo\b", "", t, flags=re.I)
    t = re.sub(rf"(^|[\s:\-–(]){NAMES[composer]}(?=$|[\s:\-–).,])", " ", t, flags=re.I)  # the composer's name, anywhere
    # the catalogue number goes in the subtitle; what an emptied bracket leaves behind goes too
    t = re.sub(r"\(?\b(BWV|BMV|S\.?|Op\.?)\s*(Anh\.?\s*)?\d+[a-z]?(\s*[.,/]?\s*(No\.?|Nr\.?|n)?\s*\d+\b)?\)?", " ", t, flags=re.I)
    t = re.sub(r"\(\s*[^()]*?[-–]\s*\)|\(\s*\)|\[\s*\]|\(\s*$|^\s*\)", " ", t)
    t = re.sub(r"\s\)|\(\s", " ", t)
    t = re.sub(r"\s*[-–:]\s*(?=[-–:]|$)", "", t)
    t = re.sub(r"\s+", " ", t).strip(" -–:.,/")
    if t[:1].islower():
        t = t[:1].upper() + t[1:]
    return t


def subtitle_of(title):
    cat = catalogue(title)
    if not cat:
        return ""
    kind = {"bwv": "BWV", "s": "S.", "op": "Op."}[cat[0]]
    after = title[title.find(str(cat[1])) + len(str(cat[1])):][:14]
    n = re.search(r"^\s*(?:,|/|\.)?\s*(?:no\.?|nr\.?|n)\s*(\d+)\b|^\.(\d+)\b|^/(\d+)\b", after, re.I)
    number = n and next(g for g in n.groups() if g)
    return f"{kind} {'Anh. ' if cat[2] else ''}{cat[1]}" + (f" No. {number}" if number else "")


def select(csv_path):
    csv.field_size_limit(10 ** 8)
    best = {}
    with open(csv_path, encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            if row["subset:no_license_conflict"] != "True" or row["license"] not in ("publicdomain", "cc-zero"):
                continue
            composer_field = row["composer_name"] if row["composer_name"] != "NA" else ""
            title = row["title"] if row["title"] != "NA" else row["song_name"]
            who = composer_field or title
            composer = next((name for name, rx in COMPOSERS.items() if rx.search(who)), None)
            if not composer or NOT_THEIRS.search(who) or NOT_THEIRS.search(title):
                continue
            tracks = row["tracks"].split("-") if row["tracks"] not in ("", "NA") else []
            if not tracks or any(t not in ("0", "1", "2") for t in tracks):
                continue  # every part a piano
            if row["has_lyrics"] == "True" or EXCLUDE.search(title) or EXCLUDE.search(composer_field):
                continue
            if int(float(row["n_notes"] or 0)) < 50 or not is_piano_work(composer, title):
                continue
            rating, views = float(row["rating"] or 0), int(float(row["n_views"] or 0))
            if rating < 4.5 and views < 300:
                continue
            clean = clean_title(title, composer)
            if not re.search(r"[A-Za-z]{3}", clean) or re.search(r"[Ѐ-ӿ]", title):
                continue  # untitled, or titled in another script (a second upload of a work listed under its own name)
            key = (composer, identity(composer, clean, title))
            if not key[1] or re.fullmatch(r"(prel\w*|praelud\w*|fug\w*|minuet\w*|menuet\w*|invenci\w*)( [ivx\d]+)?", key[1]):
                continue  # "Prelude I", "Fuga", "Minuet": which one?
            score = (rating, math.log10(views + 1))
            if key not in best or score > best[key]["score"]:
                score_id = re.search(r"(\d+)\.json$", row["metadata"])
                best[key] = dict(composer=composer, title=clean, subtitle=subtitle_of(title), mxl=row["mxl"], score=score,
                                 license=row["license"], score_id=score_id[1] if score_id else "?", source_title=title)
    pieces = []
    for (composer, _), b in best.items():
        text = (b["source_title"] + " " + b["subtitle"]).lower()
        collection = next((c for c, rx in COLLECTIONS[composer] if re.search(rx, text, re.I)), "Other works")
        licence = "public domain" if b["license"] == "publicdomain" else "CC0"
        pieces.append({
            "composer": composer, "collection": collection, "title": b["title"], "subtitle": b["subtitle"], "mxl": b["mxl"],
            "credit": f"MuseScore score {b['score_id']} ({licence}), from PDMX (Long et al., CC BY 4.0).",
        })
    order = {c: i for comp in COLLECTIONS.values() for i, (c, _) in enumerate(comp)}
    pieces.sort(key=lambda p: (p["composer"], order.get(p["collection"], 99), p["subtitle"], p["title"].lower()))
    return pieces


def main():
    args = sys.argv[1:]
    folder = pathlib.Path(args[args.index("--downloads") + 1]) if "--downloads" in args else pathlib.Path("E:/modding/downloads/pdmx")
    if "--list" in args:
        pieces = select(folder / "PDMX.csv")
        for composer in COMPOSERS:
            mine = [p for p in pieces if p["composer"] == composer]
            print(f"\n{composer}: {len(mine)}")
            for p in mine:
                print(f"  {p['collection'][:26]:26} {p['title'][:66]:66} {p['subtitle']}")
        return 0
    download(folder)
    pieces = select(folder / "PDMX.csv")
    wanted = {p["mxl"].lstrip("./"): p for p in pieces}
    for composer in COMPOSERS:
        (LIBRARY / composer).mkdir(parents=True, exist_ok=True)
        for old in (LIBRARY / composer).glob("*.mxl"):
            old.unlink()  # a previous selection
    print(f"taking {len(wanted)} scores out of mxl.tar.gz ...")
    with tarfile.open(folder / "mxl.tar.gz", "r|gz") as archive:
        for member in archive:
            name = member.name.lstrip("./")
            p = wanted.get(name)
            if p is None or not member.isfile():
                continue
            target = LIBRARY / p["composer"] / pathlib.PurePosixPath(name).name
            target.write_bytes(archive.extractfile(member).read())
            p["file"] = f"Library/{p['composer']}/{target.name}"
    for composer in COMPOSERS:
        mine = [{k: p[k] for k in ("file", "composer", "collection", "title", "subtitle", "credit")}
                for p in pieces if p["composer"] == composer and "file" in p]
        (LIBRARY / composer / "catalog.json").write_text(json.dumps({"pieces": mine}, ensure_ascii=False, indent=1), encoding="utf-8")
        (LIBRARY / composer / "LICENSE.txt").write_text(
            "Scores from PDMX: A Large-Scale Public Domain MusicXML Dataset (Long, Novack, Berg-Kirkpatrick, McAuley),\n"
            "https://zenodo.org/records/15571083, CC BY 4.0. Each score was uploaded to MuseScore and marked public domain\n"
            "or CC0 by its uploader; only scores without a licence conflict are used. See catalog.json for each score.\n",
            encoding="utf-8")
        print(f"{composer}: {len(mine)} scores")
    return subprocess.run([sys.executable, "-I", str(pathlib.Path(__file__).with_name("check_library.py")), "--prune", *COMPOSERS]).returncode


if __name__ == "__main__":
    sys.exit(main())
