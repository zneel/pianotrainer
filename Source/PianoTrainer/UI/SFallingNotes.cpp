#include "SFallingNotes.h"

#include "PianoStyle.h"
#include "PianoViewSettings.h"
#include "SPianoKeyboard.h"

#include "PianoPlayer.h"
#include "PianoScore.h"

#include "Algo/BinarySearch.h"
#include "Rendering/DrawElements.h"

namespace FallingNotesImpl
{
	struct FTone
	{
		FLinearColor Top, Bottom, Rim, Glow;
	};
	// right hand gold, left hand copper, played green, missed red
	const FTone Gold{ PianoStyle::Hex(TEXT("FFD98A")), PianoStyle::Hex(TEXT("E89A1A")), PianoStyle::Hex(TEXT("FFF0C8")), PianoStyle::Hex(TEXT("F2A21E")) };
	const FTone Copper{ PianoStyle::Hex(TEXT("FFB37A")), PianoStyle::Hex(TEXT("D9561E")), PianoStyle::Hex(TEXT("FFE0C4")), PianoStyle::Hex(TEXT("E8642A")) };
	const FTone Played{ PianoStyle::Hex(TEXT("B7F7C8")), PianoStyle::Hex(TEXT("2FBF5A")), PianoStyle::Hex(TEXT("E6FFEE")), PianoStyle::Hex(TEXT("3EDC70")) };
	const FTone Missed{ PianoStyle::Hex(TEXT("FFB0A8")), PianoStyle::Hex(TEXT("D93A30")), PianoStyle::Hex(TEXT("FFE0DC")), PianoStyle::Hex(TEXT("F05252")) };

	static void Gradient(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Pos, const FVector2f& Size,
		const FLinearColor& Top, const FLinearColor& Bottom, float Radius = 0.f)
	{
		if (Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		TArray<FSlateGradientStop> Stops;
		Stops.Emplace(FVector2f(0.f, 0.f), Top);
		Stops.Emplace(Size, Bottom);
		FSlateDrawElement::MakeGradient(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), MoveTemp(Stops), Orient_Horizontal,
			ESlateDrawEffect::None, FVector4f(FMath::Min(Radius, FMath::Min(Size.X, Size.Y) * 0.5f)));
	}

	static void Box(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Pos, const FVector2f& Size, const FLinearColor& Color, float Radius)
	{
		if (Size.X <= 0.f || Size.Y <= 0.f)
		{
			return;
		}
		const float R = FMath::Min(Radius, FMath::Min(Size.X, Size.Y) * 0.5f);
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)), PianoStyle::Rounded(FLinearColor::White, FMath::RoundToFloat(R)), ESlateDrawEffect::None, Color);
	}

	static void Outline(FSlateWindowElementList& Out, int32 Layer, const FGeometry& Geometry, const FVector2f& Pos, const FVector2f& Size, const FLinearColor& Color, float Radius)
	{
		if (Size.X <= 2.f || Size.Y <= 2.f)
		{
			return;
		}
		const float R = FMath::Min(Radius, FMath::Min(Size.X, Size.Y) * 0.5f);
		FSlateDrawElement::MakeBox(Out, Layer, Geometry.ToPaintGeometry(Size, FSlateLayoutTransform(Pos)),
			PianoStyle::Rounded(FLinearColor::Transparent, FMath::RoundToFloat(R), FLinearColor::White, 1.f), ESlateDrawEffect::None, Color);
	}
}

void SFallingNotes::Construct(const FArguments& InArgs)
{
	Player = InArgs._Player;
	Settings = InArgs._Settings;
}

void SFallingNotes::Burst(float X, float Width, const FLinearColor& Color, int32 Count)
{
	if (Settings && !Settings->bSparks)
	{
		return;
	}
	FFlare& Flare = Flares.AddDefaulted_GetRef();
	Flare.X = X;
	Flare.Width = Width;
	Flare.Color = Color;
	for (int32 Index = 0; Index < Count && Sparks.Num() < 700; ++Index)
	{
		FSpark& S = Sparks.AddDefaulted_GetRef();
		S.Position = FVector2f(X + Random.FRandRange(-Width * 0.4f, Width * 0.4f), Random.FRandRange(0.f, 4.f));
		S.Velocity = FVector2f(Random.FRandRange(-60.f, 60.f), Random.FRandRange(60.f, 240.f));
		S.Life = Random.FRandRange(0.35f, 0.8f);
		S.Size = Random.FRandRange(1.5f, 3.2f);
		S.Color = FMath::Lerp(Color, PianoStyle::Hex(TEXT("FFF3D6")), Random.FRandRange(0.f, 0.7f));
	}
}

void SFallingNotes::Tick(const FGeometry& Geometry, const double CurrentTime, const float DeltaTime)
{
	using namespace FallingNotesImpl;
	for (int32 Index = Sparks.Num() - 1; Index >= 0; --Index)
	{
		FSpark& S = Sparks[Index];
		S.Age += DeltaTime;
		if (S.Age >= S.Life)
		{
			Sparks.RemoveAtSwap(Index);
			continue;
		}
		S.Velocity.Y -= 420.f * DeltaTime; // falls back
		S.Velocity *= FMath::Pow(0.3f, DeltaTime);
		S.Position += S.Velocity * DeltaTime;
	}
	for (int32 Index = Flares.Num() - 1; Index >= 0; --Index)
	{
		Flares[Index].Age += DeltaTime;
		if (Flares[Index].Age > 0.3f)
		{
			Flares.RemoveAtSwap(Index);
		}
	}

	// Bursts: the app's notes when they reach the rail, the player's notes when they are played.
	const UPianoPlayer* P = Player.Get();
	if (!P || !P->GetScore().IsValid() || LastWidth <= 0.f)
	{
		return;
	}
	const TArray<FPianoSessionNote>& Notes = P->GetPlayNotes();
	const double Now = P->GetSongMs();
	if (LandedSerial != P->GetScoreSerial() || Landed.Num() != Notes.Num())
	{
		Landed.Init(false, Notes.Num());
		LandedSerial = P->GetScoreSerial();
	}
	else if (Now < LastNowMs - 50.0)
	{
		for (int32 Index = Algo::LowerBoundBy(Notes, Now - 1.0, &FPianoSessionNote::OnMs); Index < Notes.Num(); ++Index)
		{
			Landed[Index] = false; // jumped back: these can burst again
		}
	}
	const uint8 Hands = uint8(P->GetHands());
	const bool bListen = P->GetListenMode();
	const bool bByHand = !Settings || Settings->bColorByHand;
	for (int32 Index = Algo::LowerBoundBy(Notes, Now - 3000.0, &FPianoSessionNote::OnMs); Index < Notes.Num() && Notes[Index].OnMs <= Now + 60.0; ++Index)
	{
		const FPianoSessionNote& N = Notes[Index];
		if (Landed[Index] || N.bTied || N.Pitch < SPianoKeyboard::LowestPitch || N.Pitch > SPianoKeyboard::HighestPitch)
		{
			continue;
		}
		const bool bUser = !bListen && (uint8(N.Hand) & Hands) != 0;
		const bool bHit = P->GetNoteState(N.SourceIndex) == EPianoNoteState::Hit;
		if ((bUser && bHit) || (!bUser && N.OnMs <= Now && N.OnMs > Now - 200.0))
		{
			Landed[Index] = true;
			float X0 = 0.f, X1 = 0.f;
			SPianoKeyboard::KeyX(N.Pitch, LastWidth, X0, X1);
			const FTone& Tone = bUser ? Played : (bByHand && N.Hand == EPianoHand::Left ? Copper : Gold);
			Burst((X0 + X1) * 0.5f, X1 - X0, Tone.Glow, bUser || bListen ? 12 : 5);
		}
	}
	LastNowMs = Now;
}

int32 SFallingNotes::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Out, int32 Layer, const FWidgetStyle&, bool) const
{
	using namespace FallingNotesImpl;
	const FVector2f Size = Geometry.GetLocalSize();
	const_cast<SFallingNotes*>(this)->LastWidth = Size.X;

	// the stage: warm dark, a little light rising from the rail
	Gradient(Out, Layer, Geometry, FVector2f(0.f, 0.f), Size, PianoStyle::Hex(TEXT("0B0806")), PianoStyle::Hex(TEXT("140D08")));
	Gradient(Out, Layer + 1, Geometry, FVector2f(0.f, Size.Y * 0.7f), FVector2f(Size.X, Size.Y * 0.3f), FLinearColor(1.f, 0.6f, 0.15f, 0.f), FLinearColor(1.f, 0.55f, 0.12f, 0.07f));

	// octave lanes (at every C) and fainter F lanes
	for (int32 Pitch = 24; Pitch <= SPianoKeyboard::HighestPitch; ++Pitch)
	{
		const int32 Class = Pitch % 12;
		if (Class != 0 && Class != 5)
		{
			continue;
		}
		float X0 = 0.f, X1 = 0.f;
		SPianoKeyboard::KeyX(Pitch, Size.X, X0, X1);
		FSlateDrawElement::MakeLines(Out, Layer + 1, Geometry.ToPaintGeometry(), { FVector2f(X0, 0.f), FVector2f(X0, Size.Y) }, ESlateDrawEffect::None,
			FLinearColor(1.f, 0.85f, 0.6f, Class == 0 ? 0.07f : 0.03f), false, 1.f);
	}

	const UPianoPlayer* P = Player.Get();
	const TSharedPtr<FPianoScore> Score = P ? P->GetScore() : nullptr;
	if (!Score.IsValid())
	{
		return Layer + 1;
	}
	const double Now = P->GetSongMs();
	const float Lookahead = Settings ? Settings->LookaheadMs : 2600.f;
	const double Window = double(Lookahead) * FMath::Max(0.1f, P->GetTempoScale());
	auto YOf = [&](double Ms) { return Size.Y - float((Ms - Now) / Window) * Size.Y; };

	// measure lines with their numbers
	const TArray<FPianoScoreMeasure>& Measures = Score->GetMeasures();
	const FSlateFontInfo Font = PianoStyle::Font(8, TEXT("Bold"));
	for (int32 Index = Algo::LowerBoundBy(Measures, Now, &FPianoScoreMeasure::OnMs); Index < Measures.Num() && Measures[Index].OnMs <= Now + Window; ++Index)
	{
		const float Y = YOf(Measures[Index].OnMs);
		FSlateDrawElement::MakeLines(Out, Layer + 2, Geometry.ToPaintGeometry(), { FVector2f(0.f, Y), FVector2f(Size.X, Y) }, ESlateDrawEffect::None,
			FLinearColor(1.f, 0.75f, 0.4f, 0.13f), false, 1.f);
		FSlateDrawElement::MakeText(Out, Layer + 2, Geometry.ToPaintGeometry(FVector2f(40.f, 12.f), FSlateLayoutTransform(FVector2f(6.f, Y - 13.f))),
			FString::FromInt(Index + 1), Font, ESlateDrawEffect::None, FLinearColor(1.f, 0.8f, 0.5f, 0.3f));
	}

	// the notes: halo, body, rim; the hand the app plays is dimmer
	const TArray<FPianoSessionNote>& Notes = P->GetPlayNotes();
	const uint8 Hands = uint8(P->GetHands());
	const bool bListen = P->GetListenMode();
	const bool bByHand = !Settings || Settings->bColorByHand;
	const bool bAccentDrill = P->GetDrill() == EPianoDrill::AccentThree || P->GetDrill() == EPianoDrill::AccentFour;
	const int32 Base = Layer + 3;
	for (int32 Index = Algo::LowerBoundBy(Notes, Now - 30000.0, &FPianoSessionNote::OnMs); Index < Notes.Num(); ++Index)
	{
		const FPianoSessionNote& N = Notes[Index];
		if (N.OnMs > Now + Window)
		{
			break;
		}
		if (N.OffMs <= Now || N.bTied || N.Pitch < SPianoKeyboard::LowestPitch || N.Pitch > SPianoKeyboard::HighestPitch)
		{
			continue;
		}
		const bool bUser = !bListen && (uint8(N.Hand) & Hands) != 0;
		const bool bBright = bListen || bUser; // dim only the hand the app plays while you practise the other
		const EPianoNoteState State = P->GetNoteState(N.SourceIndex);
		const FTone& Tone = bUser && State == EPianoNoteState::Hit ? Played : bUser && State == EPianoNoteState::Missed ? Missed
			: (bByHand && N.Hand == EPianoHand::Left ? Copper : Gold);
		const bool bBlack = SPianoKeyboard::IsBlackKey(N.Pitch);
		float X0 = 0.f, X1 = 0.f;
		SPianoKeyboard::KeyX(N.Pitch, Size.X, X0, X1);
		const float Inset = bBlack ? 0.f : FMath::Max(1.f, (X1 - X0) * 0.08f);
		X0 += Inset;
		X1 -= Inset;
		if (bAccentDrill && N.bAccent)
		{
			X0 -= 1.5f;
			X1 += 1.5f;
		}
		const float Bottom = FMath::Min(Size.Y, YOf(N.OnMs));
		const float Top = FMath::Max(0.f, FMath::Min(Bottom - 4.f, YOf(N.OffMs)));
		const FVector2f Pos(X0, Top), Box2(X1 - X0, Bottom - Top);
		const float Alpha = bBright ? 1.f : 0.4f;
		const bool bSounding = N.OnMs <= Now;
		const int32 L = Base + (bBlack ? 3 : 0); // black-key notes over white-key ones

		// halo (bloom), stronger while it sounds
		const float HaloA = (bSounding ? 0.30f : 0.14f) * Alpha;
		Box(Out, L, Geometry, Pos - FVector2f(5.f), Box2 + FVector2f(10.f), Tone.Glow.CopyWithNewOpacity(HaloA * 0.5f), 8.f);
		Box(Out, L, Geometry, Pos - FVector2f(2.5f), Box2 + FVector2f(5.f), Tone.Glow.CopyWithNewOpacity(HaloA), 5.f);
		// body: light at the top, deep at the bottom; brighter while sounding
		const FLinearColor TopC = (bSounding ? FMath::Lerp(Tone.Top, FLinearColor::White, 0.35f) : Tone.Top).CopyWithNewOpacity(Alpha);
		const FLinearColor BotC = (bSounding ? Tone.Top : Tone.Bottom).CopyWithNewOpacity(Alpha);
		Gradient(Out, L + 1, Geometry, Pos, Box2, TopC, BotC, 3.f);
		Outline(Out, L + 2, Geometry, Pos, Box2, Tone.Rim.CopyWithNewOpacity(0.85f * Alpha), 3.f);
	}

	// the rail glow and the flares where notes land
	const int32 FxLayer = Base + 6;
	Gradient(Out, FxLayer, Geometry, FVector2f(0.f, Size.Y - 14.f), FVector2f(Size.X, 14.f), FLinearColor(1.f, 0.65f, 0.2f, 0.f), FLinearColor(1.f, 0.7f, 0.25f, 0.28f));
	FSlateDrawElement::MakeLines(Out, FxLayer, Geometry.ToPaintGeometry(), { FVector2f(0.f, Size.Y - 1.f), FVector2f(Size.X, Size.Y - 1.f) }, ESlateDrawEffect::None,
		PianoStyle::GoldGlow, false, 2.f);
	for (const FFlare& F : Flares)
	{
		const float T = F.Age / 0.3f;
		const float W = F.Width * (1.6f + 1.4f * T);
		const float H = 26.f * (1.f - 0.4f * T);
		Box(Out, FxLayer + 1, Geometry, FVector2f(F.X - W * 0.5f, Size.Y - H), FVector2f(W, H), F.Color.CopyWithNewOpacity(0.35f * (1.f - T)), 10.f);
	}
	for (const FSpark& S : Sparks)
	{
		const float T = S.Age / S.Life;
		const FVector2f At(S.Position.X, Size.Y - S.Position.Y);
		Box(Out, FxLayer + 2, Geometry, At - FVector2f(S.Size * 1.8f), FVector2f(S.Size * 3.6f), S.Color.CopyWithNewOpacity(0.25f * (1.f - T)), 4.f);
		Box(Out, FxLayer + 3, Geometry, At - FVector2f(S.Size * 0.5f), FVector2f(S.Size), S.Color.CopyWithNewOpacity(1.f - T), 1.f);
	}
	return FxLayer + 3;
}
