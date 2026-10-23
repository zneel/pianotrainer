"""Checks that a recorded run really made sound at the score's note onsets.

Reads Saved/PianoLogs/autotest.wav (master output of a play-along -recordwav run) and the session CSV, computes
RMS in 10 ms frames, finds energy onsets and checks one lands within 20 ms of every played note time.

usage: py -I tools/wav_onsets.py [wav] [session.csv]
"""
import csv
import pathlib
import struct
import sys
import wave

ROOT = pathlib.Path(__file__).resolve().parent.parent
LOGS = ROOT / "Saved" / "PianoLogs"


def read_wav(path):
    with wave.open(str(path), "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        samples = struct.unpack(f"<{n * ch}h", raw)
        scale = 32768.0
    elif width == 4:
        samples = struct.unpack(f"<{n * ch}i", raw)
        scale = 2147483648.0
    else:
        raise SystemExit(f"unsupported sample width {width}")
    mono = [sum(samples[i * ch:(i + 1) * ch]) / (ch * scale) for i in range(n)]
    return mono, rate


def main():
    wav = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else LOGS / "autotest.wav"
    csv_path = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else max(LOGS.glob("session_*.csv"), key=lambda p: p.stat().st_mtime)
    mono, rate = read_wav(wav)
    hop = rate // 100
    rms = []
    for i in range(0, len(mono) - hop, hop):
        block = mono[i:i + hop]
        rms.append((sum(x * x for x in block) / hop) ** 0.5)
    peak = max(rms) if rms else 0.0
    print(f"{wav.name}: {len(mono) / rate:.2f} s, {rate} Hz, peak RMS {peak:.4f}, silent frames {sum(r < 1e-4 for r in rms)}/{len(rms)}")

    # energy onsets: frame RMS at least 1.6x the previous 50 ms average and above a floor
    onsets = []
    for i in range(5, len(rms)):
        prev = sum(rms[i - 5:i]) / 5
        if rms[i] > 0.02 * peak and rms[i] > 1.6 * prev + 1e-5 and (not onsets or i - onsets[-1] > 8):
            onsets.append(i)
    print(f"audio onsets: {len(onsets)}; first at {[round(o / 100, 2) for o in onsets[:12]]} s")

    rows = list(csv.DictReader(open(csv_path)))
    # Recording starts with playback, so in play-along mode audio time = song time + a constant output latency.
    notes = sorted({round(float(r["song_ms"])) for r in rows if r["event"] == "hit"})
    expected = [ms / 1000 for ms in notes if ms >= 100]  # onsets in the first 50 ms can't be detected
    if not expected or not onsets:
        print("nothing to compare")
        return
    gaps = sorted(min((o / 100 - t for o in onsets), key=abs) for t in expected)
    latency = gaps[len(gaps) // 2]
    within = sum(abs(g - latency) <= 0.02 for g in gaps)
    print(f"note onsets checked: {len(expected)}, audio onset within 20 ms (after {latency * 1000:.0f} ms latency): "
          f"{within} ({100 * within / len(expected):.0f}%), worst {max(abs(g - latency) for g in gaps) * 1000:.0f} ms")


if __name__ == "__main__":
    main()
