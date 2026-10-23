"""Counts click-like discontinuities in recordings (voice cut-offs, buffer underruns).

A click shows up as a spike in the second difference x[n] - 2x[n-1] + x[n-2] far above its surroundings.
For each 20 ms block: peak second difference vs. the median block level of its +-100 ms neighbourhood.

usage: py -I tools/wav_clicks.py a.wav [b.wav ...]
"""
import array
import sys
import wave


def scan(path, ratio=25.0, floor=1500):
    with wave.open(path, "rb") as w:
        ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
        data = array.array("h")
        data.frombytes(w.readframes(n))
    left = data[0::ch]
    e = [abs(left[i] - 2 * left[i - 1] + left[i - 2]) for i in range(2, len(left))]
    win = rate // 50
    means = [sum(e[k:k + win]) / win for k in range(0, len(e) - win, win)]
    clicks = []
    for b in range(len(means)):
        peak = max(e[b * win:(b + 1) * win])
        around = sorted(means[max(0, b - 5):b + 6])
        ref = around[len(around) // 2]
        if peak > floor and peak > ratio * max(ref, 1.0):
            clicks.append(b * win / rate)
    return len(left) / rate, clicks


def main():
    for path in sys.argv[1:]:
        seconds, clicks = scan(path)
        print(f"{path}: {seconds:.1f} s, {len(clicks)} click-like spikes"
              + (f", first at {[round(t, 2) for t in clicks[:8]]} s" if clicks else ""))


if __name__ == "__main__":
    main()
