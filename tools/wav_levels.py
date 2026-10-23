"""Level and tone summary of recorded runs, for A/B-ing piano patches.

For each wav: duration, channels, peak, clipped samples, RMS while sounding, stereo width (side/mid energy),
RMS frequency (brightness) and the share of 10 ms frames above -45 dBFS (how much sound sustains).

usage: py -I tools/wav_levels.py a.wav [b.wav ...]
"""
import math
import struct
import sys
import wave


def read(path):
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    fmt = {2: "h", 4: "i"}[width]
    scale = float(1 << (8 * width - 1))
    data = struct.unpack(f"<{n * ch}{fmt}", raw)
    chans = [[data[i] / scale for i in range(c, len(data), ch)] for c in range(ch)]
    return chans, rate


def rms_frequency(x, rate):
    # sqrt of the power spectrum's second moment, from the first difference: a one-pass brightness measure
    num = sum((b - a) ** 2 for a, b in zip(x, x[1:]))
    den = sum(v * v for v in x)
    return rate / (2 * math.pi) * math.sqrt(num / den) if den else 0.0


def main():
    for path in sys.argv[1:]:
        chans, rate = read(path)
        left = chans[0]
        right = chans[1] if len(chans) > 1 else chans[0]
        n = len(left)
        peak = max(max(abs(v) for v in c) for c in chans)
        clipped = sum(abs(v) >= 0.999 for c in chans for v in c)
        mid = [(a + b) / 2 for a, b in zip(left, right)]
        side = [(a - b) / 2 for a, b in zip(left, right)]
        loud = [m for m in mid if abs(m) > 1e-3]
        rms = math.sqrt(sum(v * v for v in loud) / max(1, len(loud)))
        width = math.sqrt(sum(v * v for v in side) / max(1e-12, sum(v * v for v in mid)))
        hop = rate // 100
        frames = [math.sqrt(sum(v * v for v in mid[i:i + hop]) / hop) for i in range(0, n - hop, hop)]
        sustained = sum(f > 10 ** (-45 / 20) for f in frames) / max(1, len(frames))
        print(f"{path}: {n / rate:.1f} s, {len(chans)} ch, peak {20 * math.log10(max(peak, 1e-9)):.1f} dBFS, "
              f"clipped {clipped}, RMS(sounding) {20 * math.log10(max(rms, 1e-9)):.1f} dBFS, "
              f"stereo side/mid {width:.3f}, RMS frequency {rms_frequency(mid, rate):.0f} Hz, "
              f"frames above -45 dBFS {100 * sustained:.0f}%")


if __name__ == "__main__":
    main()
