// Where a piece's phrases begin, from what the score shows, for cutting practice sections at phrase boundaries.
// Bernhard plans the chunks from the music's structure (phrases, motifs, what repeats, where it is hard) and only
// goes smaller than that when the seven-times test says a chunk is too big.
//
// Every quarter-note beat is a possible start. It scores for:
//  - a rest in the melody (the right hand) just before a new melody note in this beat;
//  - a long last note before it (half again the local note spacing, and at least a beat);
//  - a slur or phrase mark ending before it / starting in it;
//  - a fermata just before it;
//  - a double bar, the end or start of a repeat;
//  - a cadence: the phrase's last bass note on the tonic or dominant of the key;
//  - a passage that comes back note for note (both copies start here);
//  - a bar line (a little).
// It scores against cutting through sounding notes with nothing starting.
#pragma once

#include "CoreMinimal.h"

class FPianoScore;

struct PIANOKIT_API FPianoPhrases
{
	static FPianoPhrases Analyse(const FPianoScore& Score);

	// Section starts (beat indices) from 0 up to and including NumBeats: phrase-sized sections - about two to eight
	// bars, smaller where the music is dense - cut where the score shows phrases begin.
	TArray<int32> Segment() const;
	// Where to cut the beats [First, End) in two: the strongest phrase start near the middle; INDEX_NONE if too small.
	int32 BestSplit(int32 First, int32 End) const;
	// Beat at a song time (the beat it falls in), and the beat nearest to it.
	int32 BeatAt(double Ms) const;
	int32 NearestBeat(double Ms) const;
	bool IsBarStart(int32 Beat) const { return BarStart.IsValidIndex(Beat) && BarStart[Beat]; }

	int32 NumBeats = 0;
	TArray<double> BeatMs;     // NumBeats + 1 entries (the last is the end of the piece)
	TArray<float> Strength;    // per beat: how strongly a phrase starts there
	TArray<FString> Why;       // per beat: the cues ("rest, long note, slur")
	TArray<int32> Onsets;      // per beat: distinct onset times, both hands
	TArray<bool> BarStart;     // per beat
	TArray<float> BeatsPerBar; // per beat: quarter beats in its bar

	// Length (in four-quarter bars, whatever the metre) and distinct onsets of the beats [First, End).
	float BarsIn(int32 First, int32 End) const { return BarPrefix[End] - BarPrefix[First]; }
	int32 OnsetsIn(int32 First, int32 End) const { return OnsetPrefix[End] - OnsetPrefix[First]; }

private:
	float SectionCost(int32 First, int32 End) const;
	TArray<float> BarPrefix;
	TArray<int32> OnsetPrefix;
};
