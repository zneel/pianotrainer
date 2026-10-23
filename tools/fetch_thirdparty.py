"""Downloads (or verifies) the third-party sources that ThirdParty/build_win64.bat and build_mac.sh compile, pinned by
SHA-256, and unpacks them into ThirdParty/src:
  Verovio 6.3.0   (LGPL-3.0)  ~6.3 MB   https://github.com/rism-digital/verovio
  lunasvg 3.5.0   (MIT)       ~0.3 MB   https://github.com/sammycage/lunasvg
  sfizz 1.2.3     (BSD-2)     ~19.1 MB  https://github.com/sfztools/sfizz (release archive, with its bundled deps)
The build scripts then apply ThirdParty/patches and build the libraries into Plugins/PianoKit/ThirdParty.

usage: py -I tools/fetch_thirdparty.py [--verify]
"""
import hashlib
import pathlib
import sys
import tarfile
import urllib.request

SRC = pathlib.Path(__file__).resolve().parent.parent / "ThirdParty" / "src"
PACKAGES = [
    ("verovio-6.3.0.tar.gz", "https://codeload.github.com/rism-digital/verovio/tar.gz/refs/tags/version-6.3.0",
     "095a561e9bd26c97787ad49e01009576d1ca234feae3995948c5aede97a40598", "verovio-version-6.3.0"),
    ("lunasvg-3.5.0.tar.gz", "https://codeload.github.com/sammycage/lunasvg/tar.gz/refs/tags/v3.5.0",
     "1abf1472ee6c4d19797916e8cc3c2e4b628e0d81178ffac60bdb0d457e32c690", "lunasvg-3.5.0"),
    ("sfizz-1.2.3.tar.gz", "https://github.com/sfztools/sfizz/releases/download/1.2.3/sfizz-1.2.3.tar.gz",
     "a9339eac7620d7f0f6b44bdfe860680fab73e66efad4b5f15b21198dd9436822", "sfizz-1.2.3"),
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    verify_only = "--verify" in sys.argv
    SRC.mkdir(parents=True, exist_ok=True)
    ok = True
    for name, url, digest, folder in PACKAGES:
        archive = SRC / name
        if not archive.is_file() or sha256(archive) != digest:
            if verify_only:
                print(f"{name}: missing or wrong hash")
                ok = False
                continue
            print(f"{name}: downloading {url}")
            with urllib.request.urlopen(url) as r:
                archive.write_bytes(r.read())
            if sha256(archive) != digest:
                print(f"{name}: SHA-256 mismatch, refusing to unpack")
                ok = False
                continue
        if not (SRC / folder).is_dir():
            if verify_only:
                print(f"{name}: not unpacked")
                ok = False
                continue
            with tarfile.open(archive) as tar:
                tar.extractall(SRC, filter="data")
            print(f"{name}: unpacked into {SRC / folder}")
        else:
            print(f"{name}: ok")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
