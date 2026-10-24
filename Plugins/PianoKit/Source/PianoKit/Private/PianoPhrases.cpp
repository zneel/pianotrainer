#include "PianoPhrases.h"

#include "PianoScore.h"

#include "Algo/BinarySearch.h"

namespace PianoPhrasesImpl
{
	static constexpr double Eps = 2.0; // ms: onsets this close are together

	// what a cue adds to a beat's phrase-start strength
	static constexpr float BarWeight = 0.5f;
	static constexpr float RestWeight = 2.f;      // the melody rests before a new note in this beat
	static constexpr float PauseWeight = 1.f;     // ... and so does everything else
	static constexpr float LongWeight = 1.5f;     // the melody's last note before it is long (twice its neighbours)
	static constexpr float SlurEndWeight = 1.f;
	static constexpr float SlurStartWeight = 1.f;
	static constexpr float FermataWeight = 3.f;
	static constexpr float DoubleBarWeight = 2.5f;
	static constexpr float RepeatWeight = 3.f;
	static constexpr float CadenceWeight = 1.f;   // phrase ends on the tonic in the bass
	static constexpr float HalfCadenceWeight = 0.5f; // ... on the dominant
	static constexpr float ReturnWeight = 2.f;    // a passage that comes back note for note starts here
	static constexpr float CutSoundWeight = -1.5f; // nothing starts here and notes sound across it

	// what a section costs: a base (fewer, longer sections unless the music says otherwise), its length in bars
	// (two to four are best, up to eight), and its density (Bernhard's chunks get smaller where it is dense)
	static constexpr float SectionBase = 1.2f;
	static constexpr float DenseOnsets = 32.f;
	static constexpr float DensityWeight = 0.08f;
	static constexpr float MaxBars = 8.25f;

	static float LengthCost(float Bars)
	{
		return Bars < 0.75f ? 3.f : Bars < 1.5f ? 1.f : Bars <= 4.25f ? 0.f : 0.4f * (Bars - 4.25f);
	}
}

int32 FPianoPhrases::BeatAt(double Ms) const
{
	if (NumBeats == 0)
	{
		return 0;
	}
	const int32 Index = Algo::UpperBound(BeatMs, Ms + PianoPhrasesImpl::Eps) - 1;
	return FMath::Clamp(Index, 0, NumBeats - 1);
}

int32 FPianoPhrases::NearestBeat(double Ms) const
{
	int32 Beat = BeatAt(Ms);
	if (BeatMs.IsValidIndex(Beat + 1) && FMath::Abs(BeatMs[Beat + 1] - Ms) < FMath::Abs(Ms - BeatMs[Beat]))
	{
		++Beat;
	}
	return FMath::Clamp(Beat, 0, NumBeats);
}

FPianoPhrases FPianoPhrases::Analyse(const FPianoScore& Score)
{
	using namespace PianoPhrasesImpl;
	FPianoPhrases P;
	P.BeatMs = Score.GetBeatTimes();
	P.NumBeats = P.BeatMs.Num();
	const int32 N = P.NumBeats;
	if (N == 0)
	{
		P.BarPrefix.Init(0.f, 1);
		P.OnsetPrefix.Init(0, 1);
		return P;
	}
	P.BeatMs.Add(FMath::Max(Score.GetLengthMs(), P.BeatMs.Last() + 1.0));
	P.Strength.Init(0.f, N + 1);
	P.Why.SetNum(N + 1);
	P.Onsets.Init(0, N);
	P.BarStart.Init(false, N + 1);
	P.BeatsPerBar.Init(4.f, N);
	// each cue counts once per beat
	TSet<TPair<int32, FString>> Seen;
	auto Add = [&P, &Seen, N](int32 Beat, float Weight, const TCHAR* Reason)
	{
		bool bAlready = false;
		if (Beat <= 0 || Beat >= N)
		{
			return;
		}
		Seen.Add(TPair<int32, FString>(Beat, Reason), &bAlready);
		if (bAlready)
		{
			return;
		}
		P.Strength[Beat] += Weight;
		P.Why[Beat] += (P.Why[Beat].IsEmpty() ? FString() : FString(TEXT(", "))) + Reason;
	};

	// bars, bar lines
	const TArray<FPianoScoreMeasure>& Measures = Score.GetMeasures();
	TArray<int32> MeasureBeat;
	for (int32 M = 0; M < Measures.Num(); ++M)
	{
		MeasureBeat.Add(P.NearestBeat(Score.GetMeasureStartMs(M)));
	}
	MeasureBeat.Add(N);
	for (int32 M = 0; M < Measures.Num(); ++M)
	{
		const int32 First = MeasureBeat[M], End = FMath::Max(First + 1, MeasureBeat[M + 1]);
		P.BarStart[First] = true;
		for (int32 B = First; B < End && B < N; ++B)
		{
			P.BeatsPerBar[B] = float(End - First);
		}
		Add(First, BarWeight, TEXT("bar"));
		if (Measures[M].bRepeatStart)
		{
			Add(First, RepeatWeight, TEXT("repeat starts"));
		}
		if (Measures[M].bRepeatEnd)
		{
			Add(MeasureBeat[M + 1], RepeatWeight, TEXT("repeat ends"));
		}
		if (Measures[M].bDoubleBarEnd)
		{
			Add(MeasureBeat[M + 1], DoubleBarWeight, TEXT("double bar"));
		}
	}

	// the notes: all struck ones, and the melody (right hand; everything if there is no right hand)
	TArray<const FPianoScoreNote*> All, Melody;
	for (const FPianoScoreNote& Note : Score.GetNotes())
	{
		if (Note.IsStruck() && !Note.bGrace)
		{
			All.Add(&Note);
			if (Note.Hand == EPianoHand::Right)
			{
				Melody.Add(&Note);
			}
		}
	}
	if (Melody.Num() == 0)
	{
		Melody = All;
	}
	// distinct onsets: time, the top note's length there (the melody, above any inner voice), latest end so far
	struct FOnset { double Ms; double TopDur; int32 TopPitch; double OffBefore; };
	auto Distinct = [](const TArray<const FPianoScoreNote*>& Notes)
	{
		TArray<FOnset> Out;
		double LastOff = -1e12;
		for (const FPianoScoreNote* Note : Notes)
		{
			if (Out.Num() == 0 || Note->OnMs - Out.Last().Ms > Eps)
			{
				Out.Add({ Note->OnMs, Note->DurMs, Note->Pitch, LastOff });
			}
			else if (Note->Pitch >= Out.Last().TopPitch)
			{
				Out.Last().TopDur = Note->DurMs;
				Out.Last().TopPitch = Note->Pitch;
			}
			LastOff = FMath::Max(LastOff, Note->OnMs + Note->DurMs);
		}
		return Out;
	};
	// the melody line (skyline): the top note of each right-hand onset, unless a higher note is still sounding there
	// (then it is an inner voice moving under a held melody note)
	TArray<const FPianoScoreNote*> Line;
	for (int32 I = 0; I < Melody.Num();)
	{
		const double OnMs = Melody[I]->OnMs;
		const FPianoScoreNote* Top = Melody[I];
		int32 Next = I;
		for (; Next < Melody.Num() && Melody[Next]->OnMs - OnMs <= Eps; ++Next)
		{
			Top = Melody[Next]->Pitch >= Top->Pitch ? Melody[Next] : Top;
		}
		bool bUnder = false;
		for (int32 K = I - 1; K >= 0 && !bUnder && OnMs - Melody[K]->OnMs < 16000.0; --K)
		{
			bUnder = Melody[K]->Pitch > Top->Pitch && Melody[K]->OnMs + Melody[K]->DurMs > OnMs + Eps;
		}
		if (!bUnder)
		{
			Line.Add(Top);
		}
		I = Next;
	}
	const TArray<FOnset> Mel = Distinct(Line);
	const TArray<FOnset> Any = Distinct(All);
	for (const FOnset& O : Any)
	{
		++P.Onsets[P.BeatAt(O.Ms)];
	}
	auto NextOnsetAfter = [&Any](double Ms) -> double
	{
		const int32 Index = Algo::LowerBoundBy(Any, Ms + Eps, &FOnset::Ms);
		return Any.IsValidIndex(Index) ? Any[Index].Ms : -1.0;
	};
	// the melody's usual note length just before a note (its six predecessors)
	auto MedianBefore = [&Mel](int32 Index)
	{
		TArray<double> Durs;
		for (int32 I = FMath::Max(0, Index - 6); I < Index; ++I)
		{
			Durs.Add(Mel[I].TopDur);
		}
		Durs.Sort();
		return Durs.Num() > 0 ? Durs[Durs.Num() / 2] : 0.0;
	};
	// key: tonic pitch classes of the major key and its relative minor
	const int32 Fifths = Score.GetMarks().KeyFifths;
	const int32 MajorTonic = ((Fifths * 7) % 12 + 12) % 12, MinorTonic = (MajorTonic + 9) % 12;

	for (int32 B = 1; B < N; ++B)
	{
		const double T = P.BeatMs[B], BeatLen = FMath::Max(1.0, P.BeatMs[B + 1] - T);

		// a new melody note in this beat after a rest, or after a long note
		const int32 J = Algo::LowerBoundBy(Mel, T - Eps, &FOnset::Ms);
		if (J > 0 && Mel.IsValidIndex(J) && Mel[J].Ms < P.BeatMs[B + 1] - Eps)
		{
			bool bEnds = false;
			if (Mel[J].Ms - Mel[J].OffBefore >= 0.5 * BeatLen)
			{
				Add(B, RestWeight, TEXT("rest"));
				bEnds = true;
				const int32 K = Algo::LowerBoundBy(Any, Mel[J].Ms - Eps, &FOnset::Ms);
				if (Any.IsValidIndex(K) && Any[K].Ms - Any[K].OffBefore >= 0.5 * BeatLen)
				{
					Add(B, PauseWeight, TEXT("pause"));
				}
			}
			// a phrase's last note: twice the melody's usual length just before it, a beat or more, and longer than
			// the note after it (a long note that starts a phrase is not an ending)
			const FOnset& Last = Mel[J - 1];
			if (J - 1 >= 2 && Last.TopDur >= FMath::Max(2.0 * MedianBefore(J - 1), 0.95 * BeatLen) && Last.TopDur >= 1.5 * Mel[J].TopDur)
			{
				Add(B, LongWeight, TEXT("long note"));
				bEnds = true;
			}
			if (bEnds)
			{
				// cadence: the bass under the phrase's last melody note
				int32 Bass = 128;
				for (const FPianoScoreNote* Note : All)
				{
					if (Note->OnMs > Last.Ms + Eps)
					{
						break;
					}
					if (Note->OnMs + Note->DurMs > Last.Ms + Eps)
					{
						Bass = FMath::Min(Bass, Note->Pitch);
					}
				}
				const int32 Pc = Bass % 12;
				if (Bass < 128 && (Pc == MajorTonic || Pc == MinorTonic))
				{
					Add(B, CadenceWeight, TEXT("cadence"));
				}
				else if (Bass < 128 && (Pc == (MajorTonic + 7) % 12 || Pc == (MinorTonic + 7) % 12))
				{
					Add(B, HalfCadenceWeight, TEXT("half cadence"));
				}
			}
		}

		// cutting through sound: nothing starts here and something is held across
		const int32 At = Algo::LowerBoundBy(Any, T - Eps, &FOnset::Ms);
		const bool bOnset = Any.IsValidIndex(At) && Any[At].Ms <= T + Eps;
		if (!bOnset)
		{
			for (const FPianoScoreNote* Note : All)
			{
				if (Note->OnMs >= T - Eps)
				{
					break;
				}
				if (Note->OnMs + Note->DurMs > T + Eps)
				{
					Add(B, CutSoundWeight, TEXT("cuts a held note"));
					break;
				}
			}
		}
	}

	// slurs and phrase marks a beat or more long: one starts in this beat, or one ended just before it
	const FPianoScoreMarks& Marks = Score.GetMarks();
	for (const FPianoScoreSlur& Slur : Marks.Slurs)
	{
		const int32 StartBeat = P.BeatAt(Slur.StartMs);
		const int32 EndBeat = P.BeatAt(Slur.EndMs);
		if (EndBeat - StartBeat < FMath::Max(2.f, P.BeatsPerBar[StartBeat]))
		{
			continue; // shorter than a bar: articulation or a small legato, not a phrase
		}
		Add(StartBeat, SlurStartWeight, TEXT("slur starts"));
		const double Next = NextOnsetAfter(Slur.EndMs);
		if (Next >= 0.0)
		{
			Add(P.BeatAt(Next), SlurEndWeight, TEXT("slur ends"));
		}
	}
	for (const FVector2D& Fermata : Marks.Fermatas)
	{
		const double Next = NextOnsetAfter(FMath::Max(Fermata.X, Fermata.Y - 2.0 * Eps));
		if (Next >= 0.0)
		{
			Add(P.BeatAt(Next), FermataWeight, TEXT("fermata"));
		}
	}

	// passages that come back note for note: both copies start a phrase
	{
		TArray<uint32> Sig;
		TArray<bool> HasNotes;
		Sig.Init(0, Measures.Num());
		HasNotes.Init(false, Measures.Num());
		int32 NoteIndex = 0;
		for (int32 M = 0; M < Measures.Num(); ++M)
		{
			const double Start = Score.GetMeasureStartMs(M), End = Score.GetMeasureEndMs(M);
			const double Beat = FMath::Max(1.0, (End - Start) / FMath::Max(1, MeasureBeat[M + 1] - MeasureBeat[M]));
			uint32 Hash = GetTypeHash(MeasureBeat[M + 1] - MeasureBeat[M]);
			for (; NoteIndex < All.Num() && All[NoteIndex]->OnMs < End - Eps; ++NoteIndex)
			{
				const FPianoScoreNote* Note = All[NoteIndex];
				if (Note->OnMs >= Start - Eps)
				{
					Hash = HashCombine(Hash, HashCombine(GetTypeHash(FMath::RoundToInt32((Note->OnMs - Start) / Beat * 48.0)), GetTypeHash(Note->Pitch)));
					HasNotes[M] = true;
				}
			}
			Sig[M] = Hash;
		}
		for (int32 J = 1; J < Measures.Num(); ++J)
		{
			for (int32 I = 0; I < J; ++I)
			{
				if (!HasNotes[I] || Sig[I] != Sig[J] || (I > 0 && Sig[I - 1] == Sig[J - 1]))
				{
					continue; // only the start of a run
				}
				int32 K = 0;
				while (J + K < Measures.Num() && I + K < J && Sig[I + K] == Sig[J + K])
				{
					++K;
				}
				if (K >= 2)
				{
					Add(MeasureBeat[I], ReturnWeight, TEXT("comes back"));
					Add(MeasureBeat[J], ReturnWeight, TEXT("comes back"));
				}
			}
		}
	}

	P.BarPrefix.Init(0.f, N + 1);
	P.OnsetPrefix.Init(0, N + 1);
	for (int32 B = 0; B < N; ++B)
	{
		P.BarPrefix[B + 1] = P.BarPrefix[B] + 0.25f; // length in four-quarter bars, whatever the metre (two bars of 2/4 make one)
		P.OnsetPrefix[B + 1] = P.OnsetPrefix[B] + P.Onsets[B];
	}
	return P;
}

float FPianoPhrases::SectionCost(int32 First, int32 End) const
{
	using namespace PianoPhrasesImpl;
	return SectionBase + LengthCost(BarsIn(First, End)) + DensityWeight * FMath::Max(0.f, float(OnsetsIn(First, End)) - DenseOnsets);
}

TArray<int32> FPianoPhrases::Segment() const
{
	using namespace PianoPhrasesImpl;
	if (NumBeats == 0)
	{
		return { 0 };
	}
	// shortest path over the beats: a section costs SectionCost, a cut earns the phrase-start strength there
	TArray<float> Best;
	TArray<int32> From;
	Best.Init(TNumericLimits<float>::Max(), NumBeats + 1);
	From.Init(0, NumBeats + 1);
	Best[0] = 0.f;
	for (int32 End = 1; End <= NumBeats; ++End)
	{
		const float Cut = End < NumBeats ? Strength[End] : 0.f;
		for (int32 First = End - 1; First >= 0; --First)
		{
			if (BarsIn(First, End) > MaxBars && First < End - 1)
			{
				break;
			}
			const float Cost = Best[First] + SectionCost(First, End) - Cut;
			if (Cost < Best[End])
			{
				Best[End] = Cost;
				From[End] = First;
			}
		}
	}
	TArray<int32> Bounds;
	for (int32 At = NumBeats; At > 0; At = From[At])
	{
		Bounds.Insert(At, 0);
	}
	Bounds.Insert(0, 0);
	return Bounds;
}

int32 FPianoPhrases::BestSplit(int32 First, int32 End) const
{
	if (End - First < 2 || First < 0 || End > NumBeats)
	{
		return INDEX_NONE;
	}
	const float Mid = 0.5f * float(First + End);
	int32 Best = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	for (int32 B = First + 1; B < End; ++B)
	{
		const float Score = Strength[B] - 2.f * FMath::Abs(float(B) - Mid) / float(End - First);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = B;
		}
	}
	return Best;
}
