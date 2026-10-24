"""Compares how the app plays a score with and without expression: velocities (spread, melody vs the rest, accents)
from the session notes logs (-pianolog writes Saved/PianoLogs/notes.csv) and loudness from recordings (-recordwav).

usage: py -I tools/expression_report.py <notes_on.csv> <notes_off.csv> [<on.wav> <off.wav>]
"""
import csv
import math
import struct
import sys
import wave


def notes(path):
    rows = [r for r in csv.DictReader(open(path)) if r["tied"] == "0"]
    for r in rows:
        r["on"], r["pitch"], r["vel"], r["hand"] = float(r["on_ms"]), int(r["pitch"]), int(r["velocity"]), int(r["hand"])
    return rows


def melody_flags(rows):
    # the right hand's top note at each onset (as the expression model voices it, without the held-note test)
    top = {}
    for r in rows:
        if r["hand"] == 1:
            key = round(r["on"])
            top[key] = max(top.get(key, 0), r["pitch"])
    return [r["hand"] == 1 and r["pitch"] == top.get(round(r["on"])) for r in rows]


def stats(values):
    values = sorted(values)
    if not values:
        return "-"
    mean = sum(values) / len(values)
    sd = math.sqrt(sum((v - mean) ** 2 for v in values) / len(values))
    return f"mean {mean:5.1f} sd {sd:4.1f} range {values[0]}-{values[-1]}"


def loudness(path, window_ms=100):
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    fmt, scale = ("h", 32768.0) if width == 2 else ("i", 2147483648.0)
    s = struct.unpack(f"<{n * ch}{fmt}", raw)
    hop = rate * window_ms // 1000
    out = []
    for i in range(0, n - hop, hop):
        block = s[i * ch:(i + hop) * ch]
        rms = math.sqrt(sum((x / scale) ** 2 for x in block) / len(block))
        out.append(20 * math.log10(max(rms, 1e-6)))
    peak = 20 * math.log10(max(abs(x) for x in s) / scale)
    audible = sorted(v for v in out if v > -60)
    p = lambda q: audible[min(len(audible) - 1, int(q * len(audible)))] if audible else -99
    return f"peak {peak:5.1f} dBFS, 100 ms loudness p10 {p(0.1):5.1f} median {p(0.5):5.1f} p90 {p(0.9):5.1f} dB (range p90-p10 {p(0.9) - p(0.1):4.1f} dB)"


def main():
    on, off = notes(sys.argv[1]), notes(sys.argv[2])
    for name, rows in (("expressive", on), ("flat", off)):
        mel = melody_flags(rows)
        print(f"{name:10} all {stats([r['vel'] for r in rows])}")
        print(f"{'':10} melody {stats([r['vel'] for r, m in zip(rows, mel) if m])}")
        print(f"{'':10} rest   {stats([r['vel'] for r, m in zip(rows, mel) if not m])}")
    if len(sys.argv) > 4:
        print("loudness expressive:", loudness(sys.argv[3]))
        print("loudness flat:      ", loudness(sys.argv[4]))


if __name__ == "__main__":
    main()
