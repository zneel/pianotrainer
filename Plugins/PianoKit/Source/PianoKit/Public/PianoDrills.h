// Practice drills in the spirit of Alfred Cortot's working editions of the Chopin Etudes: the difficult figure is
// practised through rhythmic variants, impulse groups, block-chord positions and moving accents before it is played
// as written. Each drill re-times (or re-accents) the practised hands' notes of any score; the accompaniment stays as
// written. Notes keep their SourceIndex, and every change stays inside a beat, so the sheet cursor still lines up.
// (Cortot's own exercises are not reproduced: they are generated from the score.)
#pragma once

#include "CoreMinimal.h"
#include "PianoSession.h"

enum class EPianoDrill : uint8
{
	AsWritten,
	LongShort,    // dotted rhythm: pairs of notes long-short
	ShortLong,    // dotted rhythm: pairs short-long
	Impulses,     // each beat's notes played as one quick group, then held: thinking in gestures, not single notes
	BlockChords,  // each beat's notes struck together: learn the hand positions of arpeggios and broken chords
	AccentThree,  // accent every 3rd note (cuts across the beat)
	AccentFour,   // accent every 4th note
	Outline,      // only each beat's first notes: the skeleton under the figuration (Bernhard: learn the outline first)
	Count
};

struct PIANOKIT_API FPianoDrills
{
	static FText GetName(EPianoDrill Drill);
	static FText GetHint(EPianoDrill Drill);

	// Notes (sorted by OnMs) -> the same notes re-timed for the drill, sorted by OnMs again. Only notes of Hands change.
	// Beats: quarter-note times from FPianoScore::GetBeatTimes.
	static TArray<FPianoSessionNote> Apply(EPianoDrill Drill, const TArray<FPianoSessionNote>& Notes, const TArray<double>& Beats, EPianoHand Hands);
};
