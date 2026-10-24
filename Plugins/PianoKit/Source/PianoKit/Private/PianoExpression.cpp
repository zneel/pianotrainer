#include "PianoExpression.h"

#include "PianoPhrases.h"
#include "PianoScore.h"

#include "Algo/BinarySearch.h"

namespace PianoExpressionImpl
{
	static constexpr double Eps = 2.0;           // ms: onsets this close are together
	static constexpr int32 DefaultLevel = 70;     // before the first dynamic: between mp and mf
	static constexpr int32 HairpinStep = 14;      // a hairpin with no dynamic after it moves this far
	static constexpr int32 Melody = 8, InnerVoice = -8, HeldVoice = 6, Bass = 2, Figure = 0, LowFigure = -4;
	static constexpr int32 Accent = 14, Marcato = 20, Sforzando = 18, Tenuto = 5, Staccato = -4, Grace = -10;
	static constexpr int32 Downbeat = 5, Beat = 2, PhraseEnd = -5, Spread = 2;
}

TArray<uint8> FPianoExpression::Velocities(const FPianoScore& Score)
{
	using namespace PianoExpressionImpl;
	const TArray<FPianoScoreNote>& Notes = Score.GetNotes();
	const FPianoScoreMarks& Marks = Score.GetMarks();
	TArray<uint8> Out;
	Out.Init(80, Notes.Num());
	if (Notes.Num() == 0)
	{
		return Out;
	}
	const FPianoPhrases Phrases = FPianoPhrases::Analyse(Score);

	// dynamic level at a time: the last dynamic before it (the first one also covers the bars before it, if close)
	TArray<FPianoScoreDynamic> Levels;
	for (const FPianoScoreDynamic& D : Marks.Dynamics)
	{
		if (D.Level > 0)
		{
			Levels.Add(D);
		}
	}
	const int32 Opening = Levels.Num() > 0 && Levels[0].Ms < Score.GetMeasureEndMs(1) ? Levels[0].Level : DefaultLevel;
	auto LevelAt = [&](double Ms)
	{
		int32 Level = Opening;
		for (const FPianoScoreDynamic& D : Levels)
		{
			if (D.Ms > Ms + Eps)
			{
				break;
			}
			Level = D.Level;
		}
		return Level;
	};
	auto NextLevelAfter = [&](double Ms, double& OutMs)
	{
		for (const FPianoScoreDynamic& D : Levels)
		{
			if (D.Ms > Ms + Eps)
			{
				OutMs = D.Ms;
				return D.Level;
			}
		}
		OutMs = TNumericLimits<double>::Max();
		return -1;
	};
	auto LastLevelMs = [&](double Ms)
	{
		double At = -TNumericLimits<double>::Max();
		for (const FPianoScoreDynamic& D : Levels)
		{
			At = D.Ms <= Ms + Eps ? D.Ms : At;
		}
		return At;
	};
	// hairpins: those leading from one dynamic to the next share the way (a "poco a poco cresc." drawn as three reaches
	// the fff at the third; the last runs on to the dynamic if it comes just after); one that leads nowhere swells or
	// fades by a step and comes back over as long again
	auto HairpinDelta = [&](double Ms)
	{
		double Delta = 0.0;
		for (const FPianoScoreHairpin& H : Marks.Hairpins)
		{
			if (H.StartMs > Ms + Eps)
			{
				break;
			}
			double NextMs = 0.0;
			const int32 Next = NextLevelAfter(H.StartMs, NextMs);
			if (NextMs <= Ms + Eps)
			{
				continue; // a new dynamic has taken over
			}
			const int32 From = LevelAt(H.StartMs);
			const double Span = FMath::Max(1.0, H.EndMs - H.StartMs);
			if (Next > 0 && Next != From && (Next > From) == H.bCrescendo)
			{
				const double DynMs = LastLevelMs(H.StartMs);
				int32 Shared = 0;
				for (const FPianoScoreHairpin& Other : Marks.Hairpins)
				{
					Shared += Other.bCrescendo == H.bCrescendo && Other.StartMs >= DynMs - Eps && Other.StartMs < NextMs - Eps ? 1 : 0;
				}
				const double EndMs = NextMs <= H.EndMs + 1500.0 ? NextMs : H.EndMs;
				Delta += double(Next - From) / FMath::Max(1, Shared) * FMath::Clamp((Ms - H.StartMs) / FMath::Max(1.0, EndMs - H.StartMs), 0.0, 1.0);
			}
			else
			{
				const double Along = FMath::Clamp((Ms - H.StartMs) / Span, 0.0, 1.0);
				const double Back = FMath::Clamp((Ms - H.EndMs) / Span, 0.0, 1.0);
				Delta += (H.bCrescendo ? HairpinStep : -HairpinStep) * (Along - Back);
			}
		}
		return Delta;
	};
	// hairpins add up (a "poco a poco cresc." drawn as three), but never past the next written dynamic, nor more than
	// a step when that dynamic does not lie their way
	auto DynamicAt = [&](double Ms)
	{
		const int32 Level = LevelAt(Ms);
		double NextMs = 0.0;
		const int32 Next = NextLevelAfter(Ms, NextMs);
		const double Up = Next > Level ? Next - Level : HairpinStep;
		const double Down = Next > 0 && Next < Level ? Level - Next : HairpinStep;
		return Level + FMath::Clamp(HairpinDelta(Ms), -Down, Up);
	};

	// voicing: the right hand's top line (unless under a held higher note) and its clearly held notes; the left
	// hand's lowest note at each onset
	TArray<int32> Role;
	Role.Init(0, Notes.Num());
	TArray<int32> Right, Left;
	for (int32 I = 0; I < Notes.Num(); ++I)
	{
		(Notes[I].Hand == EPianoHand::Left ? Left : Right).Add(I);
	}
	for (int32 G = 0; G < Right.Num();)
	{
		const double OnMs = Notes[Right[G]].OnMs;
		int32 Top = Right[G], End = G;
		for (; End < Right.Num() && Notes[Right[End]].OnMs - OnMs <= Eps; ++End)
		{
			Top = Notes[Right[End]].Pitch > Notes[Top].Pitch ? Right[End] : Top;
		}
		bool bUnder = false;
		for (int32 K = G - 1; K >= 0 && !bUnder && OnMs - Notes[Right[K]].OnMs < 16000.0; --K)
		{
			const FPianoScoreNote& Held = Notes[Right[K]];
			bUnder = Held.Pitch > Notes[Top].Pitch && Held.OnMs + Held.DurMs > OnMs + Eps;
		}
		// a chord's top note sings over the rest; a single note of a figuration (arpeggio, scale) stays even, unless
		// it moves under a held melody note
		for (int32 K = G; K < End; ++K)
		{
			Role[Right[K]] = bUnder ? InnerVoice : Right[K] != Top ? InnerVoice : End - G > 1 ? Melody : Figure;
		}
		G = End;
	}
	// a right-hand note held twice as long as those around it (a melody in an inner voice) is voiced too
	for (int32 G = 0; G < Right.Num(); ++G)
	{
		TArray<double> Around;
		for (int32 K = FMath::Max(0, G - 8); K < FMath::Min(Right.Num(), G + 9); ++K)
		{
			Around.Add(Notes[Right[K]].DurMs);
		}
		Around.Sort();
		if (Role[Right[G]] != Melody && Notes[Right[G]].DurMs >= 2.0 * Around[Around.Num() / 2])
		{
			Role[Right[G]] = HeldVoice;
		}
	}
	for (int32 G = 0; G < Left.Num();)
	{
		const double OnMs = Notes[Left[G]].OnMs;
		int32 Low = Left[G], End = G;
		for (; End < Left.Num() && Notes[Left[End]].OnMs - OnMs <= Eps; ++End)
		{
			Low = Notes[Left[End]].Pitch < Notes[Low].Pitch ? Left[End] : Low;
		}
		// a chord's bass; in a single-note figuration, the lowest note of the beat is the bass and the rest go under
		bool bLowestOfBeat = true;
		if (End - G == 1 && Phrases.NumBeats > 0)
		{
			const int32 B = Phrases.BeatAt(OnMs);
			for (int32 K = 0; K < Left.Num() && bLowestOfBeat; ++K)
			{
				const FPianoScoreNote& Other = Notes[Left[K]];
				bLowestOfBeat = Other.OnMs < Phrases.BeatMs[B] - Eps || Other.OnMs >= Phrases.BeatMs[B + 1] - Eps || Other.Pitch >= Notes[Low].Pitch;
			}
		}
		for (int32 K = G; K < End; ++K)
		{
			Role[Left[K]] = Left[K] != Low ? InnerVoice : End - G > 1 || bLowestOfBeat ? Bass : LowFigure;
		}
		G = End;
	}

	// phrase endings: the beat before a clear phrase start
	TSet<int32> EndBeats;
	for (int32 B = 1; B < Phrases.NumBeats; ++B)
	{
		if (Phrases.Strength[B] >= 3.f)
		{
			EndBeats.Add(B - 1);
		}
	}

	for (int32 I = 0; I < Notes.Num(); ++I)
	{
		const FPianoScoreNote& N = Notes[I];
		double V = DynamicAt(N.OnMs) + Role[I];
		V += (N.Artic & ArticMarcato) ? Marcato : (N.Artic & ArticAccent) ? Accent : 0;
		V += (N.Artic & ArticTenuto) ? Tenuto : 0;
		V += (N.Artic & (ArticStaccato | ArticStaccatissimo)) ? Staccato : 0;
		V += N.bGrace ? Grace : 0;
		for (const FPianoScoreDynamic& D : Marks.Dynamics)
		{
			if (D.bAccent && FMath::Abs(D.Ms - N.OnMs) <= 20.0)
			{
				V += Sforzando;
				break;
			}
		}
		if (Phrases.NumBeats > 0)
		{
			const int32 B = Phrases.BeatAt(N.OnMs);
			if (FMath::Abs(Phrases.BeatMs[B] - N.OnMs) <= Eps)
			{
				V += Phrases.IsBarStart(B) ? Downbeat : Beat;
			}
			V += EndBeats.Contains(B) && Role[I] == Melody ? PhraseEnd : 0;
		}
		V += int32(((uint32(I) * 2654435761u) >> 16) % (2 * Spread + 1)) - Spread; // fixed per note, not periodic
		Out[I] = uint8(FMath::Clamp(FMath::RoundToInt32(V), 18, 122));
	}
	return Out;
}
