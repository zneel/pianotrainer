#include "PianoDrills.h"

#include "Algo/BinarySearch.h"

#define LOCTEXT_NAMESPACE "PianoDrills"

namespace PianoDrillsImpl
{
	// One onset of one hand: the notes struck together (a chord or a single note).
	struct FOnset
	{
		double OnMs = 0.0;
		TArray<int32, TInlineAllocator<6>> Notes;
	};

	static constexpr double ChordMs = 25.0;

	// Onsets of one hand inside [BeatStart, BeatEnd).
	static TArray<TArray<FOnset>> OnsetsPerBeat(const TArray<FPianoSessionNote>& Notes, const TArray<double>& Beats, EPianoHand Hand)
	{
		TArray<TArray<FOnset>> Out;
		Out.SetNum(Beats.Num());
		for (int32 Index = 0; Index < Notes.Num(); ++Index)
		{
			const FPianoSessionNote& N = Notes[Index];
			if (N.Hand != Hand || N.bTied || N.bGrace)
			{
				continue;
			}
			const int32 Beat = Algo::UpperBound(Beats, N.OnMs + 0.5) - 1;
			if (!Out.IsValidIndex(Beat))
			{
				continue;
			}
			TArray<FOnset>& List = Out[Beat];
			if (List.Num() == 0 || N.OnMs - List.Last().OnMs > ChordMs)
			{
				List.AddDefaulted_GetRef().OnMs = N.OnMs;
			}
			List.Last().Notes.Add(Index);
		}
		return Out;
	}

	static double BeatEnd(const TArray<double>& Beats, int32 Beat, double Fallback)
	{
		return Beats.IsValidIndex(Beat + 1) ? Beats[Beat + 1] : Fallback;
	}

	static void MoveOnset(TArray<FPianoSessionNote>& Notes, const FOnset& Onset, double NewOnMs)
	{
		for (int32 Index : Onset.Notes)
		{
			const double Length = Notes[Index].OffMs - Notes[Index].OnMs;
			Notes[Index].OnMs = NewOnMs;
			Notes[Index].OffMs = NewOnMs + Length;
		}
	}

	// After re-timing, a note must not ring past the next onset of its hand (that is where the hand moves on).
	static void TrimToNext(TArray<FPianoSessionNote>& Notes, const TArray<FOnset>& Onsets, double EndMs)
	{
		for (int32 I = 0; I < Onsets.Num(); ++I)
		{
			const double Next = Onsets.IsValidIndex(I + 1) ? Notes[Onsets[I + 1].Notes[0]].OnMs : EndMs;
			for (int32 Index : Onsets[I].Notes)
			{
				FPianoSessionNote& N = Notes[Index];
				N.OffMs = FMath::Max(N.OnMs + 30.0, FMath::Min(N.OffMs, Next));
			}
		}
	}
}

FText FPianoDrills::GetName(EPianoDrill Drill)
{
	switch (Drill)
	{
	case EPianoDrill::LongShort: return LOCTEXT("LongShort", "Dotted: long–short");
	case EPianoDrill::ShortLong: return LOCTEXT("ShortLong", "Dotted: short–long");
	case EPianoDrill::Impulses: return LOCTEXT("Impulses", "Impulses");
	case EPianoDrill::BlockChords: return LOCTEXT("BlockChords", "Block chords");
	case EPianoDrill::AccentThree: return LOCTEXT("AccentThree", "Accent every 3rd");
	case EPianoDrill::AccentFour: return LOCTEXT("AccentFour", "Accent every 4th");
	case EPianoDrill::Outline: return LOCTEXT("Outline", "Outline");
	default: return LOCTEXT("AsWritten", "As written");
	}
}

FText FPianoDrills::GetHint(EPianoDrill Drill)
{
	switch (Drill)
	{
	case EPianoDrill::LongShort: return LOCTEXT("LongShortHint", "Pairs of notes in a dotted rhythm. Lean on the long note, flick the short one.");
	case EPianoDrill::ShortLong: return LOCTEXT("ShortLongHint", "The same pairs reversed: the short note now leads into the long one.");
	case EPianoDrill::Impulses: return LOCTEXT("ImpulsesHint", "Each beat as one quick gesture, then rest on its last note. Think in groups, not notes.");
	case EPianoDrill::BlockChords: return LOCTEXT("BlockChordsHint", "Each beat's notes together as one chord: learn the hand positions before the motion.");
	case EPianoDrill::AccentThree: return LOCTEXT("AccentThreeHint", "Accent every third note so the stress walks across the beat. Keep the rest light.");
	case EPianoDrill::AccentFour: return LOCTEXT("AccentFourHint", "Accent every fourth note. Even fingers, clear accents.");
	case EPianoDrill::Outline: return LOCTEXT("OutlineHint", "Only the first notes of each beat: the harmony and line under the figuration. Learn it first and make it musical; the figuration fills it in later.");
	default: return LOCTEXT("AsWrittenHint", "The music as written.");
	}
}

TArray<FPianoSessionNote> FPianoDrills::Apply(EPianoDrill Drill, const TArray<FPianoSessionNote>& Notes, const TArray<double>& Beats, EPianoHand Hands)
{
	using namespace PianoDrillsImpl;
	TArray<FPianoSessionNote> Out = Notes;
	if (Drill == EPianoDrill::AsWritten || Drill >= EPianoDrill::Count || Beats.Num() == 0)
	{
		return Out;
	}
	const double PieceEnd = Notes.Num() > 0 ? Notes.Last().OffMs : Beats.Last();

	for (EPianoHand Hand : { EPianoHand::Right, EPianoHand::Left })
	{
		if ((uint8(Hands) & uint8(Hand)) == 0)
		{
			continue;
		}
		const TArray<TArray<FOnset>> PerBeat = OnsetsPerBeat(Out, Beats, Hand);

		if (Drill == EPianoDrill::Outline)
		{
			// keep each beat's first onset; the rest of the figuration is left out (silent, not asked for)
			for (const TArray<FOnset>& Onsets : PerBeat)
			{
				for (int32 I = 1; I < Onsets.Num(); ++I)
				{
					for (int32 Index : Onsets[I].Notes)
					{
						Out[Index].bTied = true;
					}
				}
				if (Onsets.Num() > 0)
				{
					for (int32 Index : Onsets[0].Notes)
					{
						const int32 Beat = Algo::UpperBound(Beats, Out[Index].OnMs + 0.5) - 1;
						Out[Index].OffMs = FMath::Max(Out[Index].OffMs, BeatEnd(Beats, Beat, PieceEnd)); // held through the beat
					}
				}
			}
			continue;
		}

		if (Drill == EPianoDrill::AccentThree || Drill == EPianoDrill::AccentFour)
		{
			const int32 Every = Drill == EPianoDrill::AccentThree ? 3 : 4;
			int32 Count = 0;
			for (const TArray<FOnset>& Onsets : PerBeat)
			{
				for (const FOnset& Onset : Onsets)
				{
					const bool bAccent = Count++ % Every == 0;
					for (int32 Index : Onset.Notes)
					{
						Out[Index].bAccent = bAccent;
						Out[Index].Velocity = bAccent ? 100 : 62;
					}
				}
			}
			continue;
		}

		for (int32 Beat = 0; Beat < PerBeat.Num(); ++Beat)
		{
			const TArray<FOnset>& Onsets = PerBeat[Beat];
			if (Onsets.Num() < 2)
			{
				continue;
			}
			const double Start = Onsets[0].OnMs;
			const double End = BeatEnd(Beats, Beat, PieceEnd);
			if (Drill == EPianoDrill::LongShort || Drill == EPianoDrill::ShortLong)
			{
				const double Share = Drill == EPianoDrill::LongShort ? 0.75 : 0.25;
				for (int32 I = 0; I + 1 < Onsets.Num(); I += 2)
				{
					const double PairStart = Onsets[I].OnMs;
					const double PairEnd = Onsets.IsValidIndex(I + 2) ? Onsets[I + 2].OnMs : End;
					MoveOnset(Out, Onsets[I + 1], PairStart + (PairEnd - PairStart) * Share);
				}
			}
			else if (Drill == EPianoDrill::Impulses)
			{
				// the whole beat in its first third, evenly, then hold the last note to the beat's end
				const double Span = (End - Start) * 0.34;
				for (int32 I = 1; I < Onsets.Num(); ++I)
				{
					MoveOnset(Out, Onsets[I], Start + Span * I / (Onsets.Num() - 1));
				}
				for (int32 Index : Onsets.Last().Notes)
				{
					Out[Index].OffMs = FMath::Max(Out[Index].OffMs, End);
				}
			}
			else if (Drill == EPianoDrill::BlockChords)
			{
				// one chord per beat; a pitch repeated within the beat is struck once
				TSet<int32> Pitches;
				for (const FOnset& Onset : Onsets)
				{
					for (int32 Index : Onset.Notes)
					{
						FPianoSessionNote& N = Out[Index];
						N.OnMs = Start;
						N.OffMs = End;
						if (Pitches.Contains(N.Pitch))
						{
							N.bTied = true; // already sounding in this chord
						}
						Pitches.Add(N.Pitch);
					}
				}
			}
		}

		if (Drill != EPianoDrill::BlockChords)
		{
			// rebuild the hand's onset list in the new timing and keep notes from overlapping the next one
			TArray<FOnset> All;
			for (const TArray<FOnset>& Onsets : OnsetsPerBeat(Out, Beats, Hand))
			{
				All.Append(Onsets);
			}
			TrimToNext(Out, All, PieceEnd);
		}
	}

	Out.StableSort([](const FPianoSessionNote& A, const FPianoSessionNote& B) { return A.OnMs < B.OnMs; });
	return Out;
}

#undef LOCTEXT_NAMESPACE
